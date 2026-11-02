using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Security.AccessControl;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace OnjMusic.SoundCanvas {
    public sealed class Entry { public string path; public long size; public string sha256; }
    public sealed class Manifest { public string product; public string version; public Entry[] files; }
    public sealed class Release { public string version; public string asset; public string download; public string page; public string notes; }
    public sealed class Transaction { public string id; public bool committed; public bool remove; public bool[] existed; }

    internal static class Maintenance {
        const string Product="Sound Canvas Bridge", Repository="OnjLouis/sound-canvas-bridge";
        const string ManifestName="install-manifest.json", SignatureName="install-manifest.sig";
        const int MaximumFiles=128, MaximumFile=64*1024*1024, MaximumPayload=192*1024*1024;
        const int MaximumReplyBytes=3072;
        static string Reply(object result) {
            var text=Json.Serialize(result);
            if(Encoding.UTF8.GetByteCount(text)<=MaximumReplyBytes) return text;
            return Json.Serialize(new {status="error",message="The update helper reply exceeded its limit. See the product page or installer log for details."});
        }
        static void WriteReply(object result) {
            var bytes=Encoding.UTF8.GetBytes(Reply(result)+"\n");
            using(var output=Console.OpenStandardOutput()) output.Write(bytes,0,bytes.Length);
        }
        static readonly JavaScriptSerializer Json=new JavaScriptSerializer { MaxJsonLength=1024*1024, RecursionLimit=24 };
        static readonly string Self=Assembly.GetExecutingAssembly().Location;
        static readonly string Version=FileVersionInfo.GetVersionInfo(Self).ProductVersion;
        static readonly string Updates=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"OnjMusic","SoundCanvasBridge","Updates");
        static readonly string LogFolder=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"OnjMusic","SoundCanvasBridge","Logs");
        static readonly string MachineData=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"OnjMusicSoundCanvasBridge-Installer");
        static readonly string SharedApp=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"OnjMusic",Product);
        static readonly string SharedVst=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonProgramFiles),"VST3",Product+".vst3");
        static readonly string LegacyApp=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"Programs","OnjMusic",Product);
        static string PublicKey { get { return Encoding.UTF8.GetString(Resource("WindowsUpdateKey")); } }
        static byte[] Resource(string name) {
            using(var input=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if(input==null) throw new InvalidDataException("This file does not contain an installation payload. Download the complete installer.");
                using(var output=new MemoryStream()) { input.CopyTo(output); return output.ToArray(); }
            }
        }
        static bool Admin { get { return new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator); } }
        static string Hash(byte[] bytes) { using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant(); }
        static string HashFile(string path) { using(var file=File.OpenRead(path)) using(var sha=SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant(); }
        static void Plain(string path) {
            var current=Path.GetFullPath(path);
            while(!String.IsNullOrEmpty(current)) {
                if((File.Exists(current)||Directory.Exists(current)) && (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)
                    throw new IOException("A redirected installation path was refused: "+current);
                current=Path.GetDirectoryName(current);
            }
        }
        static void Log(string text,bool machine=false) {
            try {
                if(machine) EnsureMachineData();
                string folder=machine ? MachineData : LogFolder; Plain(folder);Directory.CreateDirectory(folder);
                string file=Path.Combine(folder,"Installer.log");Plain(file);
                if(File.Exists(file) && new FileInfo(file).Length>262144) File.WriteAllText(file,"Earlier entries rotated.\r\n");
                File.AppendAllText(file,DateTimeOffset.Now.ToString("o")+" "+text.Substring(0,Math.Min(text.Length,12000))+"\r\n");
            } catch { }
        }
        static void EnsureMachineData() {
            Plain(MachineData);
            var admins=new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null);
            var system=new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null);
            if(!Directory.Exists(MachineData)) {
                var security=new DirectorySecurity();security.SetAccessRuleProtection(true,false);security.SetOwner(admins);
                var inherit=InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit;
                foreach(var sid in new[]{admins,system}) security.AddAccessRule(new FileSystemAccessRule(sid,FileSystemRights.FullControl,inherit,PropagationFlags.None,AccessControlType.Allow));
                security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid,null),FileSystemRights.ReadAndExecute,inherit,PropagationFlags.None,AccessControlType.Allow));
                Directory.CreateDirectory(MachineData,security);
            }
            var acl=Directory.GetAccessControl(MachineData);
            var owner=(SecurityIdentifier)acl.GetOwner(typeof(SecurityIdentifier));
            if(!owner.Equals(admins) && !owner.Equals(system)) throw new IOException("The installation recovery folder has an unexpected owner.");
            var writes=FileSystemRights.Write|FileSystemRights.Delete|FileSystemRights.ChangePermissions|FileSystemRights.TakeOwnership|FileSystemRights.DeleteSubdirectoriesAndFiles;
            foreach(FileSystemAccessRule rule in acl.GetAccessRules(true,true,typeof(SecurityIdentifier)))
                if(rule.AccessControlType==AccessControlType.Allow && (rule.FileSystemRights&writes)!=0 && !rule.IdentityReference.Equals(admins) && !rule.IdentityReference.Equals(system))
                    throw new IOException("The installation recovery folder permits untrusted changes.");
        }
        static DirectorySecurity SharedSecurity() {
            var security=new DirectorySecurity();security.SetAccessRuleProtection(true,false);
            var admins=new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null);security.SetOwner(admins);
            var inherit=InheritanceFlags.ContainerInherit|InheritanceFlags.ObjectInherit;
            foreach(var sid in new[]{admins,new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null)})
                security.AddAccessRule(new FileSystemAccessRule(sid,FileSystemRights.FullControl,inherit,PropagationFlags.None,AccessControlType.Allow));
            security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid,null),FileSystemRights.ReadAndExecute,inherit,PropagationFlags.None,AccessControlType.Allow));
            return security;
        }
        static bool TrustedWriter(SecurityIdentifier sid) {
            if(sid.IsWellKnown(WellKnownSidType.BuiltinAdministratorsSid)||sid.IsWellKnown(WellKnownSidType.LocalSystemSid)) return true;
            try { return sid.Translate(typeof(NTAccount)).Value=="NT SERVICE\\TrustedInstaller"; }
            catch(IdentityNotMappedException) { return false; }
        }
        static void SharedParent(string path) {
            Plain(path);
            if(!Directory.Exists(path)) Directory.CreateDirectory(path,SharedSecurity());
            var acl=Directory.GetAccessControl(path);var owner=(SecurityIdentifier)acl.GetOwner(typeof(SecurityIdentifier));
            if(!TrustedWriter(owner)) throw new IOException("The shared installation parent has an unexpected owner: "+path);
            var writes=FileSystemRights.Write|FileSystemRights.Delete|FileSystemRights.ChangePermissions|FileSystemRights.TakeOwnership|FileSystemRights.DeleteSubdirectoriesAndFiles;
            foreach(FileSystemAccessRule rule in acl.GetAccessRules(true,true,typeof(SecurityIdentifier))) {
                if((rule.PropagationFlags&PropagationFlags.InheritOnly)!=0 || rule.AccessControlType!=AccessControlType.Allow || (rule.FileSystemRights&writes)==0) continue;
                var sid=(SecurityIdentifier)rule.IdentityReference;
                if(!TrustedWriter(sid))
                    throw new IOException("The shared installation parent permits untrusted changes: "+path);
            }
        }
        static void SecureTree(string root) {
            Plain(root);Directory.SetAccessControl(root,SharedSecurity());
            foreach(var folder in Directory.GetDirectories(root)) SecureTree(folder);
            foreach(var file in Directory.GetFiles(root)) {
                Plain(file);var acl=new FileSecurity();acl.SetAccessRuleProtection(true,false);
                var admins=new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null);acl.SetOwner(admins);
                foreach(var sid in new[]{admins,new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null)}) acl.AddAccessRule(new FileSystemAccessRule(sid,FileSystemRights.FullControl,AccessControlType.Allow));
                acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid,null),FileSystemRights.ReadAndExecute,AccessControlType.Allow));
                File.SetAccessControl(file,acl);
            }
        }
        static bool Verify(byte[] bytes,byte[] signature,string key) {
            try { using(var rsa=new RSACryptoServiceProvider()) { rsa.PersistKeyInCsp=false;rsa.FromXmlString(key);return rsa.VerifyData(bytes,"SHA256",signature); } }
            catch(CryptographicException) { return false; }
        }
        static byte[] BoundedRead(string path,int maximum) {
            Plain(path);using(var input=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                if(input.Length>maximum) throw new IOException("File exceeds its size limit.");
                using(var output=new MemoryStream()) { input.CopyTo(output);return output.ToArray(); }
            }
        }
        static Manifest ReadManifest(byte[] bytes,byte[] signature,string key) {
            if(bytes.Length>1024*1024 || signature.Length>4096 || !Verify(bytes,signature,key)) throw new InvalidDataException("The installation signature is missing or invalid.");
            var manifest=Json.Deserialize<Manifest>(Encoding.UTF8.GetString(bytes));
            System.Version parsed;
            if(manifest==null || manifest.product!=Product || !ReleaseVersion(manifest.version,out parsed) || manifest.files==null || manifest.files.Length<5 || manifest.files.Length>MaximumFiles)
                throw new InvalidDataException("The installation manifest is invalid.");
            var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);long total=0;
            foreach(var entry in manifest.files) {
                if(entry==null || !SafeEntry(entry.path) || entry.size<1 || entry.size>MaximumFile || !Regex.IsMatch(entry.sha256??"","^[a-f0-9]{64}$") || !seen.Add(entry.path))
                    throw new InvalidDataException("Invalid or duplicate package file.");
                total+=entry.size;
            }
            if(total>MaximumPayload) throw new InvalidDataException("The installation is too large.");
            foreach(var name in new[]{"Application/Sound Canvas Bridge.exe","Application/SoundCanvasHelper.exe","Application/SoundCanvasMaintenance.exe","Application/Manual.html","Application/VersionHistory.html","VST3/Contents/x86_64-win/Sound Canvas Bridge.vst3","VST3/Contents/Resources/SoundCanvasHelper.exe","VST3/Contents/Resources/moduleinfo.json"})
                if(!seen.Contains(name)) throw new InvalidDataException("The package is incomplete: "+name);
            return manifest;
        }
        static bool SafeEntry(string path) {
            if(String.IsNullOrEmpty(path) || path.Length>220 || path.IndexOfAny(new[]{'\\',':','\0'})>=0 || !(path.StartsWith("Application/")||path.StartsWith("VST3/"))) return false;
            return path.Split('/').All(p=>p.Length>0 && p!="." && p!=".." && !p.EndsWith(".") && !p.EndsWith(" ") && p!=ManifestName && p!=SignatureName && !Regex.IsMatch(p,"^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])($|\\.)",RegexOptions.IgnoreCase));
        }
        sealed class Package {
            public byte[] manifest,signature,payload;public Manifest info;public string key;
            public Package(byte[] data,byte[] signed,byte[] zip,string verificationKey) {
                manifest=data;signature=signed;payload=zip;key=verificationKey;info=ReadManifest(data,signed,key);
                if(zip.Length>MaximumPayload) throw new InvalidDataException("The compressed payload is too large.");
            }
        }
        static Package EmbeddedPackage() {
            var package=new Package(Resource("Manifest"),Resource("Signature"),Resource("Payload"),PublicKey);
            if(package.info.version!=Version) throw new InvalidDataException("Installer and payload versions differ.");
            return package;
        }
        static void Extract(Package package,string[] stages) {
            using(var data=new MemoryStream(package.payload,false)) using(var zip=new ZipArchive(data,ZipArchiveMode.Read)) {
                var entries=zip.Entries.ToDictionary(e=>e.FullName,StringComparer.OrdinalIgnoreCase);
                if(entries.Count!=package.info.files.Length) throw new InvalidDataException("Unexpected archive entries.");
                foreach(var file in package.info.files) {
                    ZipArchiveEntry item;
                    if(!entries.TryGetValue(file.path,out item) || item.Length!=file.size || (item.ExternalAttributes&0x400)!=0 || ((item.ExternalAttributes>>16)&0xf000)==0xa000)
                        throw new InvalidDataException("An archive entry is invalid.");
                    int root=file.path.StartsWith("Application/") ? 0 : 1;
                    string target=Path.Combine(stages[root],file.path.Substring(file.path.IndexOf('/')+1).Replace('/',Path.DirectorySeparatorChar));
                    Plain(target);Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using(var input=item.Open()) using(var output=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                        var buffer=new byte[65536];long total=0;int count;
                        while((count=input.Read(buffer,0,buffer.Length))>0) { total+=count;if(total>file.size) throw new InvalidDataException("Archive entry exceeded its declared length.");output.Write(buffer,0,count); }
                        if(total!=file.size) throw new InvalidDataException("Archive entry is truncated.");
                    }
                    if(HashFile(target)!=file.sha256) throw new InvalidDataException("Package integrity check failed: "+file.path);
                }
            }
            for(int i=0;i<2;++i) { File.WriteAllBytes(Path.Combine(stages[i],ManifestName),package.manifest);File.WriteAllBytes(Path.Combine(stages[i],SignatureName),package.signature);ValidateTree(stages[i],i,package.key,true); }
        }
        static IEnumerable<string> Files(string root) {
            Plain(root);
            foreach(var file in Directory.GetFiles(root)) { Plain(file);yield return file; }
            foreach(var directory in Directory.GetDirectories(root)) { Plain(directory);foreach(var file in Files(directory)) yield return file; }
        }
        static Manifest ValidateTree(string root,int component,string key,bool complete) {
            Plain(root);
            var manifest=ReadManifest(BoundedRead(Path.Combine(root,ManifestName),1024*1024),BoundedRead(Path.Combine(root,SignatureName),4096),key);
            string prefix=component==0 ? "Application/" : "VST3/";
            var expected=manifest.files.Where(f=>f.path.StartsWith(prefix)).ToDictionary(f=>f.path.Substring(prefix.Length).Replace('/',Path.DirectorySeparatorChar),StringComparer.OrdinalIgnoreCase);
            var found=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var file in Files(root)) {
                string name=file.Substring(root.Length+1);
                if(name==ManifestName || name==SignatureName) continue;
                Entry entry;
                if(!expected.TryGetValue(name,out entry) || new FileInfo(file).Length!=entry.size || HashFile(file)!=entry.sha256)
                    throw new IOException("The installed folder contains an unrecognised or modified file. It was not changed: "+file);
                found.Add(name);
            }
            if(complete && expected.Keys.Any(name=>!found.Contains(name))) throw new IOException("Installed file verification failed.");
            return manifest;
        }
        static void RemoveTree(string root,int component,string key) {
            if(!Directory.Exists(root)) return;ValidateTree(root,component,key,false);
            foreach(var file in Files(root).ToArray()) File.Delete(file);
            RemoveEmpty(root);
        }
        static void RemoveEmpty(string root) { Plain(root);foreach(var sub in Directory.GetDirectories(root)) RemoveEmpty(sub);Directory.Delete(root); }
        static string Sibling(string root,string id,string suffix) {
            string component=Hash(Encoding.UTF8.GetBytes(Path.GetFullPath(root).ToUpperInvariant())).Substring(0,12);
            return Path.Combine(Path.GetDirectoryName(root),".SoundCanvasBridge-"+id+"-"+component+suffix);
        }
        static void WriteJournal(string journal,Transaction transaction) {
            string temporary=journal+".new";Plain(journal);Plain(temporary);
            File.WriteAllText(temporary,Json.Serialize(transaction),new UTF8Encoding(false));
            if(File.Exists(journal)) File.Replace(temporary,journal,null);else File.Move(temporary,journal);
        }
        static void Recover(string[] roots,string journal,string key) {
            Plain(journal);if(!File.Exists(journal)) return;
            if(new FileInfo(journal).Length>4096) throw new IOException("Recovery journal is invalid.");
            var transaction=Json.Deserialize<Transaction>(File.ReadAllText(journal));
            if(transaction==null || !Regex.IsMatch(transaction.id??"","^[a-f0-9]{32}$") || transaction.existed==null || transaction.existed.Length!=2) throw new IOException("Recovery journal is invalid.");
            if(transaction.committed && !transaction.remove) for(int i=0;i<2;++i) ValidateTree(roots[i],i,key,true);
            for(int i=1;i>=0;--i) {
                string backup=Sibling(roots[i],transaction.id,".rollback"),stage=Sibling(roots[i],transaction.id,".stage");
                Plain(roots[i]);Plain(backup);Plain(stage);
                if(transaction.committed) RemoveTree(backup,i,key);
                else if(Directory.Exists(backup)) {
                    ValidateTree(backup,i,key,true);RemoveTree(roots[i],i,key);Directory.Move(backup,roots[i]);ValidateTree(roots[i],i,key,true);
                } else if(!transaction.existed[i]) RemoveTree(roots[i],i,key);
                else if(!Directory.Exists(roots[i])) throw new IOException("A previous component is missing; recovery files were retained.");
                RemoveTree(stage,i,key);
            }
            File.Delete(journal);
        }
        static void Apply(Package package,string[] roots,string journal,bool remove=false,int failAfter=-1,bool secure=false) {
            Plain(journal);Directory.CreateDirectory(Path.GetDirectoryName(journal));Recover(roots,journal,package.key);
            var transaction=new Transaction { id=Guid.NewGuid().ToString("N"),existed=roots.Select(Directory.Exists).ToArray(),remove=remove };
            var stages=roots.Select(root=>Sibling(root,transaction.id,".stage")).ToArray();
            for(int i=0;i<2;++i) {
                Plain(roots[i]);Directory.CreateDirectory(Path.GetDirectoryName(roots[i]));
                if(transaction.existed[i]) {
                    var old=ValidateTree(roots[i],i,package.key,true);
                    if(!remove && new System.Version(old.version)>new System.Version(package.info.version)) throw new IOException("A newer version is installed. Downgrades are not allowed.");
                    foreach(var file in Files(roots[i]).Where(f=>f.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)||f.EndsWith(".vst3",StringComparison.OrdinalIgnoreCase)||f.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)))
                        using(var locked=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.None)) { }
                }
            }
            // Staging is complete and signed before either live component is moved.
            if(!remove) {
                foreach(var stage in stages) Directory.CreateDirectory(stage);
                try { Extract(package,stages);if(secure) foreach(var stage in stages) SecureTree(stage); }
                catch {
                    for(int i=0;i<2;++i) {
                        string prefix=i==0 ? "Application/" : "VST3/";
                        var allowed=new HashSet<string>(package.info.files.Where(f=>f.path.StartsWith(prefix)).Select(f=>Path.Combine(stages[i],f.path.Substring(prefix.Length).Replace('/',Path.DirectorySeparatorChar))),StringComparer.OrdinalIgnoreCase);
                        allowed.Add(Path.Combine(stages[i],ManifestName));allowed.Add(Path.Combine(stages[i],SignatureName));
                        var files=Files(stages[i]).ToArray();if(files.Any(f=>!allowed.Contains(f))) throw new IOException("Unexpected staging contents retained for inspection.");
                        foreach(var file in files) File.Delete(file);RemoveEmpty(stages[i]);
                    }
                    throw;
                }
            }
            WriteJournal(journal,transaction);
            try {
                for(int i=0;i<2;++i) {
                    if(transaction.existed[i]) Directory.Move(roots[i],Sibling(roots[i],transaction.id,".rollback"));
                    if(!remove) { Directory.Move(stages[i],roots[i]);ValidateTree(roots[i],i,package.key,true); }
                    if(i==failAfter) throw new IOException("Injected transaction failure");
                }
                transaction.committed=true;WriteJournal(journal,transaction);
            } catch { Recover(roots,journal,package.key);throw; }
            // A cleanup failure is recoverable on the next run, never a reason to undo a verified commit.
            Recover(roots,journal,package.key);
        }
        static bool ReleaseVersion(string text,out System.Version parsed) {
            parsed=null;return Regex.IsMatch(text??"","^[0-9]{1,5}\\.[0-9]{1,5}\\.[0-9]{1,5}$") && System.Version.TryParse(text,out parsed);
        }
        static byte[] Download(string url,int limit) {
            ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;
            var request=(HttpWebRequest)WebRequest.Create(url);request.UserAgent="OnjMusic-SoundCanvasBridge";request.Timeout=20000;request.ReadWriteTimeout=20000;
            using(var response=(HttpWebResponse)request.GetResponse()) {
                string host=response.ResponseUri.Host;
                if(response.ResponseUri.Scheme!="https" || !(host=="github.com" || host=="api.github.com" || host=="release-assets.githubusercontent.com" || host=="objects.githubusercontent.com")) throw new IOException("Unexpected update download destination.");
                if(response.ContentLength>limit) throw new IOException("Update response is too large.");
                using(var input=response.GetResponseStream()) using(var output=new MemoryStream()) {
                    var buffer=new byte[65536];int count;var timer=Stopwatch.StartNew();
                    while((count=input.Read(buffer,0,buffer.Length))>0) { if(output.Length+count>limit || timer.Elapsed.TotalSeconds>120) throw new IOException("Update download exceeded its limit.");output.Write(buffer,0,count); }
                    return output.ToArray();
                }
            }
        }
        static Release Select(byte[] data,string current) {
            System.Version installed;if(!ReleaseVersion(current,out installed)) throw new InvalidDataException("Invalid installed version.");
            var records=Json.DeserializeObject(Encoding.UTF8.GetString(data)) as object[];
            if(records==null) throw new InvalidDataException("Invalid release list.");
            Release selected=null;
            foreach(var item in records) {
                var record=item as Dictionary<string,object>;if(record==null || !record.ContainsKey("draft") || !record.ContainsKey("prerelease") || !Equals(record["draft"],false) || !Equals(record["prerelease"],false)) continue;
                string tag=record.ContainsKey("tag_name") ? record["tag_name"] as string : null;
                if(tag==null || !tag.StartsWith("windows-v",StringComparison.Ordinal)) continue;
                string version=tag.Substring(9);System.Version candidate;
                if(!ReleaseVersion(version,out candidate) || candidate<=installed || selected!=null && candidate<=new System.Version(selected.version)) continue;
                string name="SoundCanvasBridge-"+version+"-windows-x64.exe";
                object assets; if(!record.TryGetValue("assets",out assets) || !(assets is object[])) continue;
                var names=((object[])assets).OfType<Dictionary<string,object>>().Where(a=>a.ContainsKey("name")).Select(a=>a["name"] as string).ToArray();
                if(names.Count(n=>n==name)!=1 || names.Count(n=>n==name+".sig")!=1) continue;
                selected=new Release { version=version,asset=name,download="https://github.com/"+Repository+"/releases/download/"+tag+"/"+name,page="https://github.com/"+Repository+"/releases/tag/"+tag,notes=record.ContainsKey("body") ? (record["body"] as string??"") : "" };
                selected.notes=selected.notes.Substring(0,Math.Min(5000,selected.notes.Length));
                string original=selected.notes;int kept=original.Length;
                // Keep the whole reply below the anonymous pipe capacity, even
                // when notes contain escaped or multi-byte Unicode characters.
                while(Encoding.UTF8.GetByteCount(Json.Serialize(selected))>MaximumReplyBytes-128) {
                    kept/=2;if(kept>0 && Char.IsHighSurrogate(original[kept-1])) --kept;
                    selected.notes=original.Substring(0,kept)+"... Full release notes: "+selected.page;
                }
            }
            return selected;
        }
        static Release Latest(string current) { return Select(Download("https://api.github.com/repos/"+Repository+"/releases?per_page=100",1024*1024),current); }
        static void CleanupUpdates(string updates) {
            Plain(updates);if(!Directory.Exists(updates)) return;
            foreach(var folder in Directory.GetDirectories(updates).Take(32)) {
                if(!Regex.IsMatch(Path.GetFileName(folder),"^[a-f0-9]{32}$")) continue;
                try {
                    Plain(folder);string receipt=Path.Combine(folder,"download.json");Plain(receipt);
                    if(!File.Exists(receipt) || new FileInfo(receipt).Length>4096) continue;
                    var entry=Json.Deserialize<Entry>(File.ReadAllText(receipt));
                    if(entry==null || !Regex.IsMatch(entry.path??"","^SoundCanvasBridge-[0-9]+\\.[0-9]+\\.[0-9]+-windows-x64\\.exe$") || !Regex.IsMatch(entry.sha256??"","^[a-f0-9]{64}$")) continue;
                    string file=Path.Combine(folder,entry.path),signature=file+".sig";
                    var allowed=new[]{file,signature,receipt};
                    if(Directory.GetDirectories(folder).Length!=0 || Directory.GetFiles(folder).Any(f=>!allowed.Contains(f,StringComparer.OrdinalIgnoreCase))) continue;
                    foreach(var item in allowed) Plain(item);
                    if(File.Exists(file) && (new FileInfo(file).Length!=entry.size || HashFile(file)!=entry.sha256)) continue;
                    if(File.Exists(file)) using(var locked=new FileStream(file,FileMode.Open,FileAccess.Read,FileShare.None)) { }
                    foreach(var item in allowed) if(File.Exists(item)) File.Delete(item);
                    Directory.Delete(folder);
                } catch(Exception error) { Log("Update cleanup deferred: "+error.Message); }
            }
        }
        static object Prepare(string encoded,string current,Func<string,int,byte[]> fetch,Action<string> launch,string updates) {
            if(encoded.Length>64000) throw new InvalidDataException("Update request is too large.");
            var requested=Json.Deserialize<Release>(Encoding.UTF8.GetString(Convert.FromBase64String(encoded)));
            var release=Select(fetch("https://api.github.com/repos/"+Repository+"/releases?per_page=100",1024*1024),current);
            if(requested==null || release==null || requested.version!=release.version || requested.asset!=release.asset || requested.download!=release.download || requested.page!=release.page)
                throw new InvalidDataException("The release changed. Check for updates again.");
            CleanupUpdates(updates);Plain(updates);Directory.CreateDirectory(updates);
            string folder=Path.Combine(updates,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
            string file=Path.Combine(folder,release.asset);
            try {
                byte[] bytes=fetch(release.download,MaximumPayload),signature=Convert.FromBase64String(Encoding.ASCII.GetString(fetch(release.download+".sig",4096)).Trim());
                if(!Verify(bytes,signature,PublicKey)) throw new InvalidDataException("The update signature is missing or invalid. Nothing was installed.");
                File.WriteAllBytes(file,bytes);File.WriteAllBytes(file+".sig",signature);
                var identity=FileVersionInfo.GetVersionInfo(file);
                if(identity.ProductVersion!=release.version || identity.ProductName!=Product) throw new InvalidDataException("The signed installer identity differs from the release.");
                File.WriteAllText(Path.Combine(folder,"download.json"),Json.Serialize(new Entry { path=release.asset,size=bytes.Length,sha256=Hash(bytes) }));
                launch(file);
                return new { status="prepared",message="The signed installer is open. Choose Install when playback and plug-in hosts are ready." };
            } catch {
                foreach(var path in new[]{file,file+".sig",Path.Combine(folder,"download.json")}) if(File.Exists(path)) File.Delete(path);
                if(!Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder);
                throw;
            }
        }
        static void MigrateShortcuts() {
            foreach(var folder in new[]{Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),Environment.GetFolderPath(Environment.SpecialFolder.Startup)}) {
                string file=Path.Combine(folder,Product+".lnk");if(!File.Exists(file)) continue;Plain(file);
                dynamic shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));dynamic link=shell.CreateShortcut(file);
                string target=(string)link.TargetPath;
                if(!String.Equals(target,Path.Combine(LegacyApp,Product+".exe"),StringComparison.OrdinalIgnoreCase)) continue;
                link.TargetPath=Path.Combine(SharedApp,Product+".exe");link.WorkingDirectory=SharedApp;link.Save();
                dynamic read=shell.CreateShortcut(file);
                if(!String.Equals((string)read.TargetPath,Path.Combine(SharedApp,Product+".exe"),StringComparison.OrdinalIgnoreCase)) throw new IOException("Shortcut migration could not be verified.");
            }
        }
        static void CloseStandalone() {
            foreach(var process in Process.GetProcessesByName(Product)) using(process) {
                string file;try { file=process.MainModule.FileName; } catch { throw new IOException("Cannot inspect a running Sound Canvas Bridge. Close it before installing."); }
                if(!new[]{Path.Combine(SharedApp,Product+".exe"),Path.Combine(LegacyApp,Product+".exe")}.Contains(file,StringComparer.OrdinalIgnoreCase))
                    throw new IOException("Another bridge copy is running. Close it before installing.");
                if(process.MainWindowHandle==IntPtr.Zero) {
                    Process.Start(new ProcessStartInfo(file,"--show-controls") { UseShellExecute=true });
                    for(int retry=0;retry<20 && process.MainWindowHandle==IntPtr.Zero;++retry) { Thread.Sleep(100);process.Refresh(); }
                }
                process.CloseMainWindow();if(!process.WaitForExit(5000)) throw new IOException("The bridge is still running. Close it before installing.");
            }
        }
        static ProcessStartInfo OpenBridgeInfo() {
            return new ProcessStartInfo(Path.Combine(SharedApp,Product+".exe")) { UseShellExecute=true };
        }
        static void ShowInstaller() {
            var package=EmbeddedPackage();
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            using(var form=new Form { Text="Install "+Product+" "+Version,ClientSize=new Size(720,440),MinimumSize=new Size(620,380),StartPosition=FormStartPosition.CenterScreen }) {
                var layout=new TableLayoutPanel { Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Padding=new Padding(12) };
                layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,42));
                var details=new TextBox { Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill,AccessibleName="Installation details",TabIndex=0,
                    Text="Install "+Product+" "+Version+"\r\n\r\nStandalone: "+SharedApp+"\r\nVST3: "+SharedVst+"\r\n\r\nSave your work, stop MIDI playback and unload the bridge from audio hosts. Install will close the standalone app. Other applications will not be forced to close.\r\n\r\nBoth components are verified before replacement. Settings, NKS presets and the original Roland installation are not changed. A failed replacement restores both previous components. Administrator approval is needed for the shared folders.\r\n\r\nThe Windows package is x64. Native Windows virtual MIDI remains experimental and is not included in this download; configured system MIDI inputs remain available." };
                var buttons=new FlowLayoutPanel { Dock=DockStyle.Fill,FlowDirection=FlowDirection.LeftToRight,TabIndex=1 };
                var install=new Button { Text="&Install",AutoSize=true,TabIndex=0 };
                var open=new Button { Text="&Open Sound Canvas",AutoSize=true,Enabled=false,TabIndex=1 };
                var close=new Button { Text="&Cancel",AutoSize=true,TabIndex=2,DialogResult=DialogResult.Cancel };
                buttons.Controls.AddRange(new Control[]{install,open,close});layout.Controls.Add(details);layout.Controls.Add(buttons);form.Controls.Add(layout);
                form.AcceptButton=install;form.CancelButton=close;form.Shown+=(sender,args)=>{ details.Select(0,0);details.Focus(); };
                close.Click+=(sender,args)=>form.Close();
                bool applying=false;
                form.FormClosing+=(sender,args)=>{ if(applying) args.Cancel=true; };
                open.Click+=(sender,args)=> {
                    try {
                        Process.Start(OpenBridgeInfo());
                        form.Close();
                    } catch(Exception error) { Log(error.ToString());details.AppendText("\r\nCould not open Sound Canvas Bridge: "+error.Message);details.Focus(); }
                };
                install.Click+=async (sender,args)=> {
                    applying=true;install.Enabled=false;close.Enabled=false;details.AppendText("\r\n\r\nPreparing installation...");
                    try {
                        await Task.Run(()=> {
                            CloseStandalone();Log("Installation requested for "+Version);
                            var start=new ProcessStartInfo(Self,"--apply") { UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden };
                            using(var worker=Process.Start(start)) { worker.WaitForExit();if(worker.ExitCode!=0) throw new IOException("Installation did not complete. Previous files were retained or restored. Details: "+Path.Combine(MachineData,"Installer.log")); }
                            MigrateShortcuts();
                        });
                        details.AppendText("\r\nInstallation completed and both components verified. Your settings and presets were preserved. Rescan your host's VST3 list if needed.");
                        open.Enabled=true;close.Text="&Close";form.AcceptButton=open;open.Focus();
                    } catch(Exception error) { Log(error.ToString());details.AppendText("\r\nInstallation stopped: "+error.Message);install.Enabled=true;details.Focus(); }
                    finally { applying=false;close.Enabled=true; }
                };
                Application.Run(form);
            }
            string helper=Path.Combine(SharedApp,"SoundCanvasMaintenance.exe");
            if(File.Exists(helper) && Self.StartsWith(Updates+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))
                Process.Start(new ProcessStartInfo(helper,"--cleanup-after "+Process.GetCurrentProcess().Id) { UseShellExecute=false,CreateNoWindow=true });
        }
        static void SelfTest(string root) {
            if(Admin) throw new InvalidOperationException("Run self-tests without administrator rights.");
            var launch=OpenBridgeInfo();
            if(launch.Arguments!="" || launch.FileName!=Path.Combine(SharedApp,Product+".exe") || !launch.UseShellExecute)
                throw new Exception("Installer Open must respect the saved hidden-start preference");
            if(Encoding.UTF8.GetByteCount(Reply(new {status="error",message=new string('x',8192)}))>MaximumReplyBytes)
                throw new Exception("An oversized helper reply can block the parent output pipe");
            Plain(root);if(Directory.Exists(root)||File.Exists(root)) throw new IOException("Self-test folder must be new.");
            var trustedInstaller=(SecurityIdentifier)new NTAccount("NT SERVICE","TrustedInstaller").Translate(typeof(SecurityIdentifier));
            if(!TrustedWriter(trustedInstaller) || !TrustedWriter(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid,null)) || TrustedWriter(new SecurityIdentifier(WellKnownSidType.BuiltinUsersSid,null)) || TrustedWriter(new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid,null)))
                throw new Exception("TrustedInstaller and unprivileged-writer distinction failed");
            Directory.CreateDirectory(root);var package=EmbeddedPackage();string journal=Path.Combine(root,"journal.json");var roots=new[]{Path.Combine(root,"app"),Path.Combine(root,"plugin.vst3")};
            Apply(package,roots,journal);Apply(package,roots,journal);
            var broken=new Package(package.manifest,package.signature,package.payload.Take(package.payload.Length/2).ToArray(),package.key);
            try { Apply(broken,roots,journal);throw new Exception("Truncated payload accepted"); } catch(InvalidDataException) { }
            for(int i=0;i<2;++i) ValidateTree(roots[i],i,package.key,true);
            var interrupted=new Transaction {id=Guid.NewGuid().ToString("N"),existed=new[]{true,true}};
            var interruptedStages=roots.Select(r=>Sibling(r,interrupted.id,".stage")).ToArray();
            foreach(var stage in interruptedStages) Directory.CreateDirectory(stage);Extract(package,interruptedStages);WriteJournal(journal,interrupted);
            Directory.Move(roots[0],Sibling(roots[0],interrupted.id,".rollback"));Directory.Move(interruptedStages[0],roots[0]);
            Recover(roots,journal,package.key);
            for(int i=0;i<2;++i) ValidateTree(roots[i],i,package.key,true);
            using(var busy=new FileStream(Path.Combine(roots[1],"Contents","x86_64-win",Product+".vst3"),FileMode.Open,FileAccess.Read,FileShare.None)) {
                try { Apply(package,roots,journal);throw new Exception("Busy plug-in was overwritten"); } catch(IOException) { }
            }
            try { Apply(package,roots,journal,false,0);throw new Exception("Failure injection did not run"); } catch(IOException error) { if(error.Message!="Injected transaction failure") throw; }
            for(int i=0;i<2;++i) ValidateTree(roots[i],i,package.key,true);
            var invalid=(byte[])package.manifest.Clone();invalid[0]^=1;
            try { ReadManifest(invalid,package.signature,package.key);throw new Exception("Tampered signature accepted"); } catch(InvalidDataException) { }
            string unknown=Path.Combine(roots[0],"user-data.txt");File.WriteAllText(unknown,"retain");
            try { Apply(package,roots,journal);throw new Exception("Unknown content was overwritten"); } catch(IOException) { }
            if(File.ReadAllText(unknown)!="retain") throw new Exception("Unknown content changed");File.Delete(unknown);
            Apply(package,roots,journal,true);if(roots.Any(Directory.Exists)) throw new Exception("Removal failed");
            object release=new {draft=false,prerelease=false,tag_name="windows-v1.3.0",assets=new[]{new {name="SoundCanvasBridge-1.3.0-windows-x64.exe"},new {name="SoundCanvasBridge-1.3.0-windows-x64.exe.sig"}}};
            object mac=new {draft=false,prerelease=false,tag_name="v9.0.0",assets=new[]{new {name="SoundCanvasBridge-9.0.0-mac-universal.dmg"}}};
            var selected=Select(Encoding.UTF8.GetBytes(Json.Serialize(new[]{mac,release})),"1.2.0");
            if(selected==null || selected.version!="1.3.0" || Select(Encoding.UTF8.GetBytes(Json.Serialize(new[]{release})),"1.3.0")!=null) throw new Exception("Release-family/version filtering failed");
            var verbose=Select(Encoding.UTF8.GetBytes(Json.Serialize(new[]{new {draft=false,prerelease=false,tag_name="windows-v1.3.0",body=new string('\u00e9',5000),assets=new[]{new {name="SoundCanvasBridge-1.3.0-windows-x64.exe"},new {name="SoundCanvasBridge-1.3.0-windows-x64.exe.sig"}}}})),"1.2.0");
            string reply=Reply(new {status="available",release=verbose});
            if(Encoding.UTF8.GetByteCount(reply)>MaximumReplyBytes || !verbose.notes.Contains(verbose.page) || !reply.Contains("available")) throw new Exception("Long release notes must remain a bounded available-update reply with a full-notes link");
            string artifact="SoundCanvasBridge-"+Version+"-windows-x64.exe",url="https://github.com/"+Repository+"/releases/download/windows-v"+Version+"/"+artifact;
            var api=Encoding.UTF8.GetBytes(Json.Serialize(new[]{new {draft=false,prerelease=false,tag_name="windows-v"+Version,assets=new[]{new {name=artifact},new {name=artifact+".sig"}}}}));
            var request=new Release {version=Version,asset=artifact,download=url,page="https://github.com/"+Repository+"/releases/tag/windows-v"+Version};
            string encoded=Convert.ToBase64String(Encoding.UTF8.GetBytes(Json.Serialize(request))),updateRoot=Path.Combine(root,"Updates");
            byte[] installer=BoundedRead(Self,MaximumPayload),signedInstaller=BoundedRead(Self+".sig",4096);int launches=0;
            Func<string,int,byte[]> fetch=(address,limit)=> {
                if(address.StartsWith("https://api.github.com/")) return api;
                if(address==url) return installer;
                if(address==url+".sig") return signedInstaller;
                throw new IOException("Unexpected test URL");
            };
            Prepare(encoded,"0.0.0",fetch,path=>{ if(HashFile(path)!=Hash(installer)) throw new Exception("Update bytes differ");++launches; },updateRoot);
            CleanupUpdates(updateRoot);if(Directory.EnumerateFileSystemEntries(updateRoot).Any()) throw new Exception("Update cleanup failed");
            installer[installer.Length-1]^=1;
            try { Prepare(encoded,"0.0.0",fetch,path=>{++launches;},updateRoot);throw new Exception("Tampered installer was launched"); } catch(InvalidDataException) { }
            if(launches!=1 || Directory.EnumerateFileSystemEntries(updateRoot).Any()) throw new Exception("Update rejection/cleanup failed");
            Directory.Delete(updateRoot);Directory.Delete(root);Console.WriteLine("PASS install/repeat, payload validation, rollback/crash recovery, busy-file refusal, tamper rejection, unknown-file protection, removal, release filtering and signed-update prepare/launch/cleanup");
        }
        [STAThread] static int Main(string[] arguments) {
            try {
                if(arguments.Length==2 && arguments[0]=="--check") {
                    var release=Latest(arguments[1]);WriteReply(release==null ? (object)new { status="current",message="No newer published Windows release is available." } : new { status="available",release=release });return 0;
                }
                if(arguments.Length==2 && arguments[0]=="--prepare-base64") { WriteReply(Prepare(arguments[1],Version,Download,path=>Process.Start(new ProcessStartInfo(path) {UseShellExecute=true}),Updates));return 0; }
                if(arguments.Length==1 && arguments[0]=="--apply") {
                    if(!Admin) throw new UnauthorizedAccessException("Administrator approval is required.");
                    using(var mutex=new Mutex(false,"Global\\OnjMusicSoundCanvasBridgeInstaller")) {
                        bool acquired=false;try { try { acquired=mutex.WaitOne(0); } catch(AbandonedMutexException) { acquired=true; } if(!acquired) throw new IOException("Another installation is running.");
                            EnsureMachineData();SharedParent(Path.GetDirectoryName(SharedApp));SharedParent(Path.GetDirectoryName(SharedVst));
                            var package=EmbeddedPackage();Apply(package,new[]{SharedApp,SharedVst},Path.Combine(MachineData,"transaction.json"),false,-1,true);Log("Installed and verified "+Version,true);
                        } finally { if(acquired) mutex.ReleaseMutex(); }
                    }
                    return 0;
                }
                if(arguments.Length==2 && arguments[0]=="--self-test") { SelfTest(Path.GetFullPath(arguments[1]));return 0; }
                if(arguments.Length==2 && arguments[0]=="--cleanup-after") {
                    int pid;if(!Int32.TryParse(arguments[1],out pid)) return 2;
                    try { using(var process=Process.GetProcessById(pid)) { if(!process.MainModule.FileName.StartsWith(Updates+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase)) return 2;if(!process.WaitForExit(120000)) return 2; } } catch(ArgumentException) { }
                    CleanupUpdates(Updates);return 0;
                }
                if(arguments.Length==0) { ShowInstaller();return 0; }
                throw new ArgumentException("Supported options: --check VERSION, --prepare-base64 RELEASE, --self-test NEW-FOLDER");
            } catch(Exception error) {
                bool worker=arguments.Contains("--apply");Log(error.ToString(),worker);
                WriteReply(new { status="error",message=error.Message });
                if(arguments.Length==0) MessageBox.Show(error.Message,Product,MessageBoxButtons.OK,MessageBoxIcon.Error);
                return 1;
            }
        }
    }
}
