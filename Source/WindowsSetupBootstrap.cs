using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

namespace OnjMusic.SoundCanvas {
    // Preserve the exact version-resource contract used by existing updaters.
    // The embedded wizard supplies all interactive installer UI.
    internal static class WindowsSetupBootstrap {
        static string Quote(string value) {
            var result=new StringBuilder("\"");int slashes=0;
            foreach(char character in value) {
                if(character=='\\') { ++slashes;continue; }
                if(character=='"') result.Append('\\',slashes*2+1);
                else result.Append('\\',slashes);
                slashes=0;result.Append(character);
            }
            return result.Append('\\',slashes*2).Append('"').ToString();
        }
        [STAThread] static int Main(string[] arguments) {
            string folder=null,wizard=null;
            try {
                string root=Path.Combine(Path.GetTempPath(),"OnjMusic-SoundCanvasSetup");
                if(Directory.Exists(root) && (File.GetAttributes(root)&FileAttributes.ReparsePoint)!=0) throw new IOException("The setup temporary folder is redirected.");
                Directory.CreateDirectory(root);folder=Path.Combine(root,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
                wizard=Path.Combine(folder,"Setup.exe");
                using(var input=Assembly.GetExecutingAssembly().GetManifestResourceStream("Wizard")) {
                    if(input==null || input.Length<1 || input.Length>192*1024*1024) throw new InvalidDataException("The setup wizard is missing or invalid.");
                    using(var output=new FileStream(wizard,FileMode.CreateNew,FileAccess.Write,FileShare.None)) input.CopyTo(output);
                }
                using(var readLock=new FileStream(wizard,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                    // The wizard owns one elevation request. The wrapper remains
                    // unelevated, waits for completion, then removes its own stage.
                    string command=String.Join(" ",Array.ConvertAll(arguments,Quote));
                    using(var child=Process.Start(new ProcessStartInfo(wizard,command) { UseShellExecute=true })) {
                        child.WaitForExit();return child.ExitCode;
                    }
                }
            } catch(Exception error) {
                if(Array.Exists(arguments,arg=>arg.Equals("/VERYSILENT",StringComparison.OrdinalIgnoreCase) || arg.Equals("/SILENT",StringComparison.OrdinalIgnoreCase))) return 1;
                MessageBox.Show(error.Message,"Sound Canvas Bridge Setup",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;
            } finally {
                // Never recursively delete a shared temp folder or follow a link.
                if(wizard!=null && folder!=null) {
                    try { File.Delete(wizard);if(Directory.GetFileSystemEntries(folder).Length==0) Directory.Delete(folder); }
                    catch(IOException) { }
                    catch(UnauthorizedAccessException) { }
                }
                try {
                    string self=Assembly.GetExecutingAssembly().Location;
                    string updates=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"OnjMusic","SoundCanvasBridge","Updates");
                    string helper=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"OnjMusic","Sound Canvas Bridge","SoundCanvasMaintenance.exe");
                    if(self.StartsWith(updates+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase) && File.Exists(helper))
                        Process.Start(new ProcessStartInfo(helper,"--cleanup-after "+Process.GetCurrentProcess().Id) { UseShellExecute=false,CreateNoWindow=true });
                } catch(Exception) { /* A retained verified download is safe to clean on the next update check. */ }
            }
        }
    }
}
