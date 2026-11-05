using System;
using System.Collections.Generic;

namespace PlanetEditor
{
    /// <summary>包的备份：每次写盘前留一份 auto 备份，也可以手动打整包快照。</summary>
    internal static class PackBackup
    {
        public const string BackupFolder = "Backup";
        public const string AutoFolder = "auto";
        public const int KeepSnapshots = 10;

        /// <summary>写盘前调用：把旧内容存到 Backup/auto 里，只保留上一版。</summary>
        public static void KeepAuto(IFolder pack, string relativeDir, string fileName)
        {
            if (pack == null) return;
            try
            {
                IFile src = pack.GetFolder(relativeDir).GetFile(fileName);
                if (!src.Exists()) return;
                IFolder auto = pack.GetFolder(BackupFolder).GetFolder(AutoFolder).Create();
                IFolder dstDir = string.IsNullOrEmpty(relativeDir) ? auto : auto.GetFolder(relativeDir).Create();
                dstDir.GetFile(fileName + ".bak").WriteText(src.ReadText());
            }
            catch (Exception e) { Log.Error("auto backup: " + e.Message); }
        }

        /// <summary>手动打一个整包快照，返回时间戳目录名。</summary>
        public static string Snapshot(IFolder pack)
        {
            if (pack == null) return null;
            try
            {
                string ts = DateTime.Now.ToString("yyyyMMdd_HHmmss");
                IFolder root = pack.GetFolder(BackupFolder).Create();
                IFolder snap = root.GetFolder(ts).Create();
                CopyFolder(pack.GetFolder("Planet Data"), snap.GetFolder("Planet Data"));
                CopyFolder(pack.GetFolder("Heightmap Data"), snap.GetFolder("Heightmap Data"));
                IFile imp = pack.GetFile("Import_Settings.txt");
                if (imp.Exists()) snap.GetFile("Import_Settings.txt").WriteText(imp.ReadText());
                IFile ver = pack.GetFile("Version.txt");
                if (ver.Exists()) snap.GetFile("Version.txt").WriteText(ver.ReadText());
                Prune(root);
                Log.Info("backup snapshot " + ts);
                return ts;
            }
            catch (Exception e) { Log.Error("backup snapshot: " + e.Message); return null; }
        }

        /// <summary>把一个目录里的文本文件复制到目标目录。</summary>
        private static void CopyFolder(IFolder src, IFolder dst)
        {
            if (src == null || !src.Exists()) return;
            dst.Create();
            foreach (IFile f in src.GetFiles())
            {
                try { dst.GetFile(f.Name).WriteText(f.ReadText()); }
                catch (Exception e) { Log.Error("backup copy " + f.Name + ": " + e.Message); }
            }
        }

        /// <summary>列出所有快照的时间戳，新的在前。</summary>
        public static List<string> List(IFolder pack)
        {
            var list = new List<string>();
            if (pack == null) return list;
            try
            {
                IFolder root = pack.GetFolder(BackupFolder);
                if (!root.Exists()) return list;
                foreach (IFolder f in root.GetFolders())
                {
                    if (f.Name == AutoFolder) continue;
                    list.Add(f.Name);
                }
            }
            catch (Exception e) { Log.Error("backup list: " + e.Message); }
            list.Sort();
            list.Reverse();
            return list;
        }

        /// <summary>恢复一个快照；传 auto 表示恢复上一版自动备份。</summary>
        public static bool Restore(IFolder pack, string which)
        {
            if (pack == null) return false;
            try
            {
                IFolder root = pack.GetFolder(BackupFolder);
                if (!root.Exists()) return false;
                string name = string.IsNullOrEmpty(which) ? "" : which.Trim();
                if (name.Length == 0)
                {
                    List<string> snaps = List(pack);
                    if (snaps.Count == 0) return false;
                    name = snaps[0];
                }
                IFolder snap = root.GetFolder(name);
                if (!snap.Exists()) return false;
                RestoreFolder(snap.GetFolder("Planet Data"), pack.GetFolder("Planet Data"));
                RestoreFolder(snap.GetFolder("Heightmap Data"), pack.GetFolder("Heightmap Data"));
                IFile imp = snap.GetFile("Import_Settings.txt");
                if (imp.Exists()) pack.GetFile("Import_Settings.txt").WriteText(imp.ReadText());
                Log.Info("restored backup " + name);
                return true;
            }
            catch (Exception e) { Log.Error("backup restore: " + e.Message); return false; }
        }

        /// <summary>把备份目录里的文件写回原位置。</summary>
        private static void RestoreFolder(IFolder src, IFolder dst)
        {
            if (src == null || !src.Exists()) return;
            dst.Create();
            foreach (IFile f in src.GetFiles())
            {
                try { dst.GetFile(f.Name).WriteText(f.ReadText()); }
                catch (Exception e) { Log.Error("restore copy " + f.Name + ": " + e.Message); }
            }
        }

        /// <summary>快照太多时删掉最旧的，只留 KeepSnapshots 个。</summary>
        private static void Prune(IFolder root)
        {
            try
            {
                List<string> snaps = new List<string>();
                foreach (IFolder f in root.GetFolders())
                {
                    if (f.Name != AutoFolder) snaps.Add(f.Name);
                }
                if (snaps.Count <= KeepSnapshots) return;
                snaps.Sort();
                for (int i = 0; i < snaps.Count - KeepSnapshots; i++)
                {
                    try { root.GetFolder(snaps[i]).Delete(); }
                    catch (Exception e) { Log.Error("prune backup: " + e.Message); }
                }
            }
            catch (Exception e) { Log.Error("prune backups: " + e.Message); }
        }
    }
}
