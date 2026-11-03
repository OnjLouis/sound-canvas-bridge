using System;
using System.Diagnostics;
using System.IO;
using System.Text;

namespace OnjMusic.SoundCanvas.Tests {
#if SCB_WORKDIR_WIZARD
    internal static class DummyWizard {
        static int Main(string[] args) {
            if(args.Length<3) return 2;
            string report="Working directory: "+Environment.CurrentDirectory+Environment.NewLine;
            try {
                Directory.Move(args[0],args[1]);
                File.WriteAllText(args[2],report+"Directory replacement succeeded",Encoding.UTF8);
                return 0;
            } catch(IOException error) {
                File.WriteAllText(args[2],report+error,Encoding.UTF8);
                return 10;
            }
        }
    }
#else
    internal static class WindowsInstallerWorkingDirectoryTests {
        static string Quote(string path) { return "\""+path+"\""; }
        static int Run(string file,string arguments,string workingDirectory,string temporary) {
            var start=new ProcessStartInfo(file,arguments) {
                UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=workingDirectory
            };
            if(temporary!=null) { start.EnvironmentVariables["TEMP"]=temporary;start.EnvironmentVariables["TMP"]=temporary; }
            using(var process=Process.Start(start)) {
                if(!process.WaitForExit(45000)) {
                    process.Kill();process.WaitForExit(5000);
                    throw new IOException("Isolated test child exceeded its deadline");
                }
                return process.ExitCode;
            }
        }
        static int Main(string[] args) {
            if(args.Length!=2) return 2;
            string source=Path.GetFullPath(args[0]),root=Path.GetFullPath(args[1]);
            try {
                if(Directory.Exists(root) || File.Exists(root)) throw new IOException("Test folder must be new");
                Directory.CreateDirectory(root);
                string installed=Path.Combine(root,"Installed Application"),moved=Path.Combine(root,"Rollback"),
                    temporary=Path.Combine(root,"Temp"),report=Path.Combine(root,"Result.txt");
                Directory.CreateDirectory(installed);Directory.CreateDirectory(temporary);
                File.WriteAllText(Path.Combine(installed,"preserve.txt"),"original bytes");
                string compiler=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"Microsoft.NET","Framework64","v4.0.30319","csc.exe");
                string wizard=Path.Combine(root,"DummyWizard.exe"),bootstrap=Path.Combine(root,"Bootstrap.exe");
                string testSource=Path.Combine(source,"Tests","WindowsInstallerWorkingDirectoryTests.cs");
                if(Run(compiler,"/nologo /target:winexe /define:SCB_WORKDIR_WIZARD /out:"+Quote(wizard)+" "+Quote(testSource),root,null)!=0)
                    throw new IOException("Dummy wizard compilation failed");
                if(Run(compiler,"/nologo /target:winexe /r:System.Windows.Forms.dll /resource:"+Quote(wizard)+",Wizard /out:"+Quote(bootstrap)+" "+
                       Quote(Path.Combine(source,"Source","WindowsSetupBootstrap.cs")),root,null)!=0)
                    throw new IOException("Production bootstrap compilation failed");
                // Simulate an updater started from its application's installation directory.
                int code=Run(bootstrap,Quote(installed)+" "+Quote(moved)+" "+Quote(report)+" /VERYSILENT",installed,temporary);
                Console.WriteLine(File.Exists(report)?File.ReadAllText(report):"No dummy-wizard report");
                if(code!=0 || Directory.Exists(installed) || !Directory.Exists(moved) || File.ReadAllText(Path.Combine(moved,"preserve.txt"))!="original bytes")
                    throw new IOException("Installer inherited and locked the replaced application directory");
                string stage=Path.Combine(temporary,"OnjMusic-SoundCanvasSetup");
                if(Directory.Exists(stage) && Directory.GetFileSystemEntries(stage).Length!=0)
                    throw new IOException("Bootstrap left its temporary wizard behind");
                Directory.Delete(root,true);
                Console.WriteLine("PASS real bootstrap launched from an installed-directory fixture, replacement, exact bytes and cleanup");
                return 0;
            } catch(Exception error) { Console.Error.WriteLine(error);return 1; }
        }
    }
#endif
}
