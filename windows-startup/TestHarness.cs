namespace RebirthStartupCompat.Tests
{
    using System;
    using System.Collections.Generic;
    using System.IO;
    using System.Runtime.InteropServices;

    public sealed class FakeApi : IChildApi
    {
        private readonly Inputs inputs;
        private readonly byte[] context = new byte[] {
            0x68,0xe0,0x93,0x04,0x00,0xe8,0x01,0x78,0x07,0x00,0x83,0xc4,0x04,0xe8,0x6d,0x4d,0x01,0x00 };
        public readonly List<string> Events = new List<string>();
        public string FailAt;
        public bool BadHeader, BadContext, BadReadback, CleanupFailure;
        public uint ResumeCount = 1, Base = 0x400000;
        private int protects, contextReads;
        public FakeApi(Inputs source) { inputs = source; }
        private void Call(string name)
        {
            Events.Add(name);
            if (FailAt == name) throw new IOException("Injected failure: " + name);
        }
        public Child Create(string executable, string directory)
        {
            Call("create");
            Require(executable == inputs.Path && directory == inputs.Directory, "Explicit path and working directory");
            return new Child { Process = new IntPtr(100), Thread = new IntPtr(200), Id = 300 };
        }
        public uint ImageBase(Child child) { Call("base"); return Base; }
        public byte[] Read(Child child, long address, int count)
        {
            if (address == Base && count == inputs.Header.Length)
            {
                Call("read-header");
                byte[] bytes = (byte[])inputs.Header.Clone();
                if (BadHeader) bytes[0] ^= 1;
                return bytes;
            }
            Require(address == (long)Base + 0xa54f && count == context.Length, "Exact context read range");
            Call(contextReads++ == 0 ? "read-context" : "readback");
            byte[] result = (byte[])context.Clone();
            if ((contextReads == 1 && BadContext) || (contextReads == 2 && BadReadback)) result[0] ^= 1;
            return result;
        }
        public uint Protect(Child child, long address, int count, uint protection)
        {
            Require(address == (long)Base + 0xa554 && count == 5, "Exact five-byte protection range");
            bool initial = protects++ == 0;
            Require(protection == (initial ? 0x40 : 0x20), "Temporary protection and restoration");
            Call(initial ? "protect" : "restore");
            return initial ? 0x20U : 0x40U;
        }
        public void Write(Child child, long address, byte[] bytes)
        {
            Require(address == (long)Base + 0xa554 && bytes.Length == 5, "Exact five-byte write range");
            for (int i = 0; i < bytes.Length; i++) Require(bytes[i] == 0x90, "NOP replacement");
            // A failed OS write can be partial. Cleanup must still restore protection and terminate.
            Array.Copy(bytes, 0, context, 5, FailAt == "write" ? 2 : bytes.Length);
            Call("write");
        }
        public void Flush(Child child, long address, int count)
        {
            Require(Events.Contains("restore") && Events.Contains("readback"), "Verify after restoration, before flush");
            Call("flush");
        }
        public uint Resume(Child child)
        {
            Require(Events.Contains("flush"), "Flush before resume");
            Call("resume");
            return ResumeCount;
        }
        public void Terminate(Child child)
        {
            Require(child.Id == 300 && child.Process == new IntPtr(100), "Only the newly created child is terminated");
            Call("terminate");
            if (CleanupFailure) throw new IOException("Injected termination cleanup failure");
        }
        public void Close(IntPtr handle)
        {
            Require(handle == new IntPtr(100) || handle == new IntPtr(200), "Only owned handles are closed");
            Call(handle == new IntPtr(200) ? "close-thread" : "close-process");
        }
        public static void Require(bool value, string message)
        { if (!value) throw new Exception("Test assertion: " + message); }
    }

    public static class Harness
    {
        public static string[] Run(string executable)
        {
            var passed = new List<string>();
            using (var inputs = new Inputs(executable))
            {
                FakeApi success = new FakeApi(inputs);
                FakeApi.Require(Launcher.PrepareAndResume(success, inputs) == 300, "Return newly created process ID");
                FakeApi.Require(String.Join(",", success.Events.ToArray()) ==
                    "create,base,read-header,read-context,protect,write,restore,readback,flush,resume,close-thread,close-process",
                    "Successful exact operation order");
                passed.Add("successful operation order; exactly five child-memory bytes changed");

                string[] stages = { "create", "base", "read-header", "read-context", "protect", "write",
                    "restore", "readback", "flush", "resume" };
                foreach (string stage in stages)
                {
                    FakeApi api = new FakeApi(inputs) { FailAt = stage };
                    ExpectFailure(api, inputs);
                    CheckFailedChildCleanup(api, stage != "create");
                    if (stage == "write") FakeApi.Require(api.Events.Contains("restore"), "Restore after partial write failure");
                    passed.Add("failure at " + stage + ": owned child/handle cleanup");
                }
                for (int mode = 0; mode < 6; mode++)
                {
                    FakeApi api = new FakeApi(inputs);
                    string name;
                    if (mode == 0) { api.BadHeader = true; name = "wrong remote header"; }
                    else if (mode == 1) { api.BadContext = true; name = "wrong remote startup context"; }
                    else if (mode == 2) { api.BadReadback = true; name = "wrong modified-memory readback"; }
                    else if (mode == 3) { api.ResumeCount = 0; name = "unexpected suspend count zero"; }
                    else if (mode == 4) { api.ResumeCount = 2; name = "unexpected suspend count two"; }
                    else { api.Base = 0; name = "invalid image base"; }
                    ExpectFailure(api, inputs);
                    CheckFailedChildCleanup(api, true);
                    if (mode == 0 || mode == 1 || mode == 5)
                        FakeApi.Require(!api.Events.Contains("write"), "No write before successful validation");
                    passed.Add(name + ": refused and cleaned up");
                }
                FakeApi cleanup = new FakeApi(inputs) { FailAt = "write", CleanupFailure = true };
                Exception both = ExpectFailure(cleanup, inputs);
                FakeApi.Require(both is AggregateException, "Keep original and cleanup errors");
                CheckFailedChildCleanup(cleanup, true);
                passed.Add("termination failure: still close both handles; preserve both errors");

                FakeApi close = new FakeApi(inputs) { FailAt = "close-thread" };
                ExpectFailure(close, inputs);
                FakeApi.Require(close.Events.Contains("close-process") && !close.Events.Contains("terminate"),
                    "Attempt second close; do not terminate a successfully resumed child");
                passed.Add("close failure after successful resume: second close attempted without terminating app");
            }
            FakeApi.Require(Marshal.SizeOf(typeof(WindowsChildApi.STARTUPINFO)) == 104, "64-bit STARTUPINFO layout");
            FakeApi.Require(Marshal.OffsetOf(typeof(WindowsChildApi.STARTUPINFO), "dwFlags").ToInt32() == 60,
                "64-bit STARTUPINFO flags offset");
            FakeApi.Require(Marshal.SizeOf(typeof(WindowsChildApi.PROCESS_INFORMATION)) == 24,
                "64-bit PROCESS_INFORMATION layout");
            passed.Add("64-bit process structure sizes and field alignment");
            return passed.ToArray();
        }
        private static Exception ExpectFailure(FakeApi api, Inputs inputs)
        {
            try { Launcher.PrepareAndResume(api, inputs); }
            catch (Exception error) { return error; }
            throw new Exception("Test expected a failure");
        }
        private static void CheckFailedChildCleanup(FakeApi api, bool created)
        {
            if (created)
            {
                FakeApi.Require(api.Events.Contains("terminate"), "Terminate failed newly created child");
                FakeApi.Require(api.Events[api.Events.Count - 2] == "close-thread" &&
                    api.Events[api.Events.Count - 1] == "close-process", "Close both owned handles on failure");
            }
            else FakeApi.Require(api.Events.Count == 1, "No process actions after failed creation");
        }
    }
}
