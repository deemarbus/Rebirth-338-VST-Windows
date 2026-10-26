// Experimental: compile checks do not establish that ReBirth starts on Windows.
// No file, registry, preference, media check or checksum is modified.
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace RebirthStartupCompat
{
    public sealed class Child
    {
        public IntPtr Process, Thread;
        public uint Id;
    }

    // Kept separate so failure handling can be exercised without Windows APIs.
    public interface IChildApi
    {
        Child Create(string executable, string directory);
        uint ImageBase(Child child);
        byte[] Read(Child child, long address, int count);
        uint Protect(Child child, long address, int count, uint protection);
        void Write(Child child, long address, byte[] bytes);
        void Flush(Child child, long address, int count);
        uint Resume(Child child);
        void Terminate(Child child);
        void Close(IntPtr handle);
    }

    public sealed class Inputs : IDisposable
    {
        private FileStream executable, engine;
        public readonly string Path, Directory;
        public readonly byte[] Header;
        public readonly uint ImageSize;

        public Inputs(string path)
        {
            Path = System.IO.Path.GetFullPath(path);
            Directory = System.IO.Path.GetDirectoryName(Path);
            try
            {
                // FileShare.Read prevents other writers/deletion until launch completes.
                executable = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.Read);
                engine = new FileStream(System.IO.Path.Combine(Directory, "ReBirth.dll"),
                    FileMode.Open, FileAccess.Read, FileShare.Read);
                Verify(executable, 1594880,
                    "6a651e8164b8444a5f0639cb6a2fef2aa43c5500bd9c5288627c2030b047f876", "Rebirth.exe");
                Verify(engine, 2472960,
                    "4c3a6441a2cd50f797d99b1c5b6cf0aaf198c7ad8ca2648196b530c2e3e58b12", "ReBirth.dll");
                executable.Position = 0;
                Header = new byte[512];
                ReadAll(executable, Header);
                int pe = BitConverter.ToInt32(Header, 0x3c);
                if (Header[0] != 0x4d || Header[1] != 0x5a || pe < 0x40 || pe > Header.Length - 168 ||
                    BitConverter.ToUInt32(Header, pe) != 0x4550 ||
                    BitConverter.ToUInt16(Header, pe + 4) != 0x14c ||
                    BitConverter.ToUInt16(Header, pe + 24) != 0x10b)
                    throw new InvalidDataException("Unexpected executable header.");
                if (BitConverter.ToUInt32(Header, pe + 52) != 0x400000 ||
                    BitConverter.ToUInt32(Header, pe + 160) != 0 ||
                    BitConverter.ToUInt32(Header, pe + 164) != 0)
                    throw new InvalidDataException("Expected the stock fixed-base image without relocations.");
                ImageSize = BitConverter.ToUInt32(Header, pe + 80);
            }
            catch { Dispose(); throw; }
        }

        private static void Verify(FileStream file, long length, string expected, string name)
        {
            if (file.Length != length) throw new InvalidDataException(name + ": unsupported file size.");
            byte[] digest;
            using (SHA256 sha = SHA256.Create()) { digest = sha.ComputeHash(file); }
            string actual = BitConverter.ToString(digest).Replace("-", "").ToLowerInvariant();
            if (actual != expected) throw new InvalidDataException(name + ": unsupported SHA-256; original required.");
        }

        private static void ReadAll(Stream stream, byte[] bytes)
        {
            int done = 0;
            while (done < bytes.Length)
            {
                int count = stream.Read(bytes, done, bytes.Length - done);
                if (count == 0) throw new EndOfStreamException();
                done += count;
            }
        }

        public void Dispose()
        {
            if (engine != null) { engine.Dispose(); engine = null; }
            if (executable != null) { executable.Dispose(); executable = null; }
        }
    }

    public static class Launcher
    {
        private static readonly byte[] Context = new byte[] {
            0x68,0xe0,0x93,0x04,0x00,0xe8,0x01,0x78,0x07,0x00,0x83,0xc4,0x04,0xe8,0x6d,0x4d,0x01,0x00 };

        public static uint Start(string executable)
        {
            using (Inputs inputs = new Inputs(executable))
            {
                if (Environment.OSVersion.Platform != PlatformID.Win32NT || !Environment.Is64BitProcess)
                    throw new PlatformNotSupportedException("Use 64-bit Windows PowerShell on 64-bit Windows.");
                return PrepareAndResume(new WindowsChildApi(), inputs);
            }
        }

        // The supplied inputs hold both original files open and locked.
        public static uint PrepareAndResume(IChildApi api, Inputs inputs)
        {
            Child child = null;
            bool resumed = false;
            Exception error = null;
            uint result = 0;
            try
            {
                child = api.Create(inputs.Path, inputs.Directory);
                if (child == null || child.Process == IntPtr.Zero || child.Thread == IntPtr.Zero)
                    throw new InvalidOperationException("Invalid child process handles.");
                uint imageBase = api.ImageBase(child);
                if (imageBase == 0 || (imageBase & 0xffff) != 0 ||
                    (ulong)imageBase + inputs.ImageSize > 0x100000000UL)
                    throw new InvalidDataException("Invalid child image base.");
                // Before the loader runs, the header and this relative CALL must match the stock file.
                Equal(api.Read(child, imageBase, inputs.Header.Length), inputs.Header, "Child PE header");
                long contextAddress = (long)imageBase + 0xa54f;
                long callAddress = contextAddress + 5;
                Equal(api.Read(child, contextAddress, Context.Length), Context, "Child startup context");
                byte[] replacement = new byte[] { 0x90,0x90,0x90,0x90,0x90 };
                uint oldProtection = api.Protect(child, callAddress, replacement.Length, 0x40);
                try
                {
                    api.Write(child, callAddress, replacement);
                }
                finally
                {
                    api.Protect(child, callAddress, replacement.Length, oldProtection);
                }
                byte[] expected = (byte[])Context.Clone();
                Array.Copy(replacement, 0, expected, 5, replacement.Length);
                Equal(api.Read(child, contextAddress, expected.Length), expected, "Child changed context");
                api.Flush(child, callAddress, replacement.Length);
                if (api.Resume(child) != 1)
                    throw new InvalidOperationException("Unexpected initial thread suspend count.");
                resumed = true;
                result = child.Id;
            }
            catch (Exception problem) { error = problem; }
            finally
            {
                if (child != null)
                {
                    if (!resumed && child.Process != IntPtr.Zero)
                        Cleanup(delegate { api.Terminate(child); }, ref error);
                    if (child.Thread != IntPtr.Zero)
                        Cleanup(delegate { api.Close(child.Thread); }, ref error);
                    if (child.Process != IntPtr.Zero)
                        Cleanup(delegate { api.Close(child.Process); }, ref error);
                }
            }
            if (error != null) throw error;
            return result;
        }

        private static void Cleanup(Action action, ref Exception error)
        {
            try { action(); }
            catch (Exception problem)
            {
                error = error == null ? problem : new AggregateException(error, problem);
            }
        }

        private static void Equal(byte[] actual, byte[] expected, string label)
        {
            if (actual.Length != expected.Length) throw new InvalidDataException(label + ": wrong length.");
            for (int i = 0; i < actual.Length; i++)
                if (actual[i] != expected[i]) throw new InvalidDataException(label + ": unexpected bytes.");
        }
    }

    public sealed class WindowsChildApi : IChildApi
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct STARTUPINFO
        {
            public uint cb;
            public IntPtr lpReserved, lpDesktop, lpTitle;
            public uint dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public ushort wShowWindow, cbReserved2;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }
        [StructLayout(LayoutKind.Sequential)]
        public struct PROCESS_INFORMATION
        {
            public IntPtr hProcess, hThread;
            public uint dwProcessId, dwThreadId;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CreateProcessW(string application, StringBuilder command,
            IntPtr processSecurity, IntPtr threadSecurity, bool inheritHandles, uint flags,
            IntPtr environment, string directory, ref STARTUPINFO startup, out PROCESS_INFORMATION info);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWow64Process(IntPtr process, [MarshalAs(UnmanagedType.Bool)] out bool wow64);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] bytes,
            UIntPtr size, out UIntPtr read);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes,
            UIntPtr size, out UIntPtr written);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool VirtualProtectEx(IntPtr process, IntPtr address, UIntPtr size,
            uint protection, out uint oldProtection);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushInstructionCache(IntPtr process, IntPtr address, UIntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        private static Win32Exception Failure(string operation)
        { return new Win32Exception(Marshal.GetLastWin32Error(), operation); }

        public Child Create(string executable, string directory)
        {
            STARTUPINFO startup = new STARTUPINFO();
            startup.cb = (uint)Marshal.SizeOf(typeof(STARTUPINFO));
            PROCESS_INFORMATION info;
            if (!CreateProcessW(executable, new StringBuilder("\"" + executable + "\""),
                IntPtr.Zero, IntPtr.Zero, false, 4, IntPtr.Zero, directory, ref startup, out info))
                throw Failure("CreateProcessW");
            return new Child { Process = info.hProcess, Thread = info.hThread, Id = info.dwProcessId };
        }

        public uint ImageBase(Child child)
        {
            bool wow64;
            if (!IsWow64Process(child.Process, out wow64)) throw Failure("IsWow64Process");
            if (!wow64) throw new InvalidDataException("The child must be a 32-bit process.");
            // This exact 1998 PE has no base relocations and a fixed preferred image base.
            // The complete first 512 header bytes and startup bytes are checked before any write.
            // A different mapping fails those reads/comparisons; no address-search fallback is used.
            return 0x400000;
        }

        public byte[] Read(Child child, long address, int count)
        {
            byte[] bytes = new byte[count];
            UIntPtr read;
            if (!ReadProcessMemory(child.Process, new IntPtr(address), bytes, (UIntPtr)(uint)count, out read))
                throw Failure("ReadProcessMemory");
            if (read.ToUInt64() != (ulong)count) throw new IOException("Partial ReadProcessMemory.");
            return bytes;
        }
        public uint Protect(Child child, long address, int count, uint protection)
        {
            uint previous;
            if (!VirtualProtectEx(child.Process, new IntPtr(address), (UIntPtr)(uint)count, protection, out previous))
                throw Failure("VirtualProtectEx");
            return previous;
        }
        public void Write(Child child, long address, byte[] bytes)
        {
            UIntPtr written;
            if (!WriteProcessMemory(child.Process, new IntPtr(address), bytes, (UIntPtr)(uint)bytes.Length, out written))
                throw Failure("WriteProcessMemory");
            if (written.ToUInt64() != (ulong)bytes.Length) throw new IOException("Partial WriteProcessMemory.");
        }
        public void Flush(Child child, long address, int count)
        {
            if (!FlushInstructionCache(child.Process, new IntPtr(address), (UIntPtr)(uint)count))
                throw Failure("FlushInstructionCache");
        }
        public uint Resume(Child child)
        {
            uint previous = ResumeThread(child.Thread);
            if (previous == uint.MaxValue) throw Failure("ResumeThread");
            return previous;
        }
        public void Terminate(Child child)
        {
            if (!TerminateProcess(child.Process, 1)) throw Failure("TerminateProcess (new child only)");
            uint wait = WaitForSingleObject(child.Process, 5000);
            if (wait != 0) throw new IOException("New child did not confirm termination: " + wait + ".");
        }
        public void Close(IntPtr handle)
        {
            if (!CloseHandle(handle)) throw Failure("CloseHandle");
        }
    }
}
