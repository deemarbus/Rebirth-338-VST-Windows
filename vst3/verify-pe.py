"""Check actual build outputs, without loading the original ReBirth engine."""
import json
import struct
import sys
from pathlib import Path


def inspect(path, machine, magic, dll):
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('PE field outside file')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError('Missing MZ')
    pe, = unpack('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Missing PE')
    actual, count, _, _, _, opt_size, flags = unpack('<HHIIIHH', pe + 4)
    opt = pe + 24
    actual_magic, = unpack('<H', opt)
    if actual != machine or actual_magic != magic or bool(flags & 0x2000) != dll:
        raise ValueError('Wrong machine, optional header or DLL flag')
    sections = [unpack('<8sIIIIIIHHI', opt + opt_size + i * 40) for i in range(count)]

    def rva(value, size=1):
        for sec in sections:
            _, virtual_size, virtual_address, raw_size, raw_offset, *_ = sec
            delta = value - virtual_address
            if 0 <= delta and delta + size <= raw_size and delta < max(virtual_size, raw_size):
                offset = raw_offset + delta
                if offset + size <= len(data):
                    return offset
        raise ValueError('Unmapped export RVA')

    exports = []
    if dll:
        directory = opt + (112 if magic == 0x20b else 96)
        export_rva, export_size = unpack('<II', directory)
        if not export_rva or export_size < 40:
            raise ValueError('Missing exports')
        fields = unpack('<IIHHIIIIIII', rva(export_rva, 40))
        names, table = fields[7], fields[9]
        if names > 1024:
            raise ValueError('Unexpected export count')
        for i in range(names):
            string_rva, = unpack('<I', rva(table + i * 4, 4))
            start = rva(string_rva)
            end = data.index(b'\0', start, min(start + 256, len(data)))
            exports.append(data[start:end].decode('ascii'))
        if 'GetPluginFactory' not in exports:
            raise ValueError('VST3 factory export missing')
    return dict(path=str(path), machine=hex(actual), exports=exports)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('usage: verify-pe.py plugin.vst3 worker.exe')
    print(json.dumps({'plugin': inspect(sys.argv[1], 0x8664, 0x20b, True),
                      'worker': inspect(sys.argv[2], 0x14c, 0x10b, False)}))
