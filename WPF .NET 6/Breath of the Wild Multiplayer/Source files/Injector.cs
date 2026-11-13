using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

public static class Injector
{
    static readonly IntPtr INTPTR_ZERO = (IntPtr)0;
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint dwDesiredAccess, int bInheritHandle, uint dwProcessId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern int CloseHandle(IntPtr hObject);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    static extern IntPtr GetModuleHandle(string lpModuleName);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, IntPtr dwSize, uint flAllocationType, uint flProtect);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern int WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] buffer, uint size, int lpNumberOfBytesWritten);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
    
    public static Process Inject(string processName, string dllPath, List<Process>? Filter = null)
    {
        Process? ProcessToInject = null;
        for (int retry = 0; retry < 50; retry++)
        {
            ProcessToInject = GetProcesses(processName).
                Where(process => Filter != null ? !Filter.Any(p => p.Id == process.Id) : true).
                FirstOrDefault();
            if (ProcessToInject != null)
                break;
            System.Threading.Thread.Sleep(200);
        }

        if (ProcessToInject == null)
            throw new Exception("Failed to find Cemu process");

        if (!File.Exists(dllPath))
        {
            ProcessToInject.Kill();
            throw new Exception("Failed to find mod dll");
        }

        if (!ProcessInject((uint)ProcessToInject.Id, dllPath))
        {
            ProcessToInject.Kill();
            throw new Exception("Failed to inject dll into cemu");
        }

        return ProcessToInject;
    }

    public static Process InjectToSpecificProcess(string processName, string dllPath, Process CemuProcess)
    {
        //Process? ProcessToInject = GetProcesses(processName).
        //    Where(process => Filter != null ? !Filter.Any(p => p.Id == process.Id) : true).
        //    FirstOrDefault();

        //Debug.WriteLine(GetProcesses(processName).
        //    Where(process => Filter != null ? !Filter.Any(p => p.Id == process.Id) : true).Count());

        Process ProcessToInject = CemuProcess;

        if (ProcessToInject == null)
            throw new Exception("Failed to find Cemu process");

        if (!File.Exists(dllPath))
        {
            ProcessToInject.Kill();
            throw new Exception("Failed to find mod dll");
        }

        if (!ProcessInject((uint)ProcessToInject.Id, dllPath))
        {
            ProcessToInject.Kill();
            throw new Exception("Failed to inject dll into cemu");
        }

        return ProcessToInject;
    }

    private static bool ProcessInject(uint processId, string dllPath)
    {
        IntPtr hndProc = OpenProcess((0x2 | 0x8 | 0x10 | 0x20 | 0x400), 1, processId);

        if (hndProc == INTPTR_ZERO) { return false; }

        byte[] bytes = Encoding.ASCII.GetBytes(dllPath + "\0");

        IntPtr lpAddress = VirtualAllocEx(hndProc, (IntPtr)null, (IntPtr)bytes.Length, (0x1000 | 0x2000), 0x40);

        if (lpAddress == INTPTR_ZERO)
        {
            CloseHandle(hndProc);
            return false;
        }

        if (WriteProcessMemory(hndProc, lpAddress, bytes, (uint)bytes.Length, 0) == 0)
        {
            CloseHandle(hndProc);
            return false;
        }

        IntPtr loadlibAddy = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");

        IntPtr hThread = CreateRemoteThread(hndProc, IntPtr.Zero, 0, loadlibAddy, lpAddress, 0, IntPtr.Zero);

        if (hThread != IntPtr.Zero)
        {
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
        }

        CloseHandle(hndProc);
        return true;
    }

    public static List<Process> GetProcesses(string processName) => Process.GetProcessesByName(processName).ToList();
}
