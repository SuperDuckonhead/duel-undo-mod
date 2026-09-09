using System;
using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;

namespace WindBot.Undo
{
    internal static class PrivatePipe
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint pid);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInfo { public IntPtr Reserved, Peb, R1, R2, Pid, Parent; }
        [DllImport("ntdll.dll")]
        private static extern int NtQueryInformationProcess(IntPtr process, int kind, ref ProcessInfo info, int length, out int returned);
        internal static int ParentPid()
        {
            var info = new ProcessInfo(); int returned;
            if (NtQueryInformationProcess(Process.GetCurrentProcess().Handle, 0, ref info, Marshal.SizeOf(info), out returned) != 0)
                throw new InvalidOperationException("Cannot authenticate pipe creator");
            return info.Parent.ToInt32();
        }
        internal static string NewName()
        {
            byte[] random = new byte[24]; using (var rng = RandomNumberGenerator.Create()) rng.GetBytes(random);
            return "ygopro-undo-" + BitConverter.ToString(random).Replace("-", "").ToLowerInvariant();
        }
        internal static PipeSecurity Security()
        {
            var identity = WindowsIdentity.GetCurrent().User;
            var security = new PipeSecurity();
            security.SetAccessRuleProtection(true, false);
            security.SetOwner(identity);
            security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.NetworkSid, null), PipeAccessRights.FullControl, AccessControlType.Deny));
            security.AddAccessRule(new PipeAccessRule(identity, PipeAccessRights.FullControl, AccessControlType.Allow));
            return security;
        }
        internal static NamedPipeServerStream Server(string name)
        {
            return new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 65536, 65536, Security());
        }
        internal static void VerifyClient(NamedPipeServerStream pipe, int expectedPid)
        {
            uint pid;
            if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out pid) || pid != expectedPid)
                throw new InvalidOperationException("Unexpected private pipe client process");
        }
        internal static NamedPipeClientStream ConnectToCreator(string name)
        {
            if (!name.StartsWith("ygopro-undo-", StringComparison.Ordinal) || name.Length > 100 || name.IndexOf('\\') >= 0 || name.IndexOf('/') >= 0)
                throw new InvalidOperationException("Invalid private pipe name");
            var pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.None, TokenImpersonationLevel.Identification);
            try
            {
                pipe.Connect(10000); uint pid;
                if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out pid) || pid != ParentPid())
                    throw new InvalidOperationException("Private pipe server is not the creating process");
                return pipe;
            }
            catch { pipe.Dispose(); throw; }
        }
    }
}