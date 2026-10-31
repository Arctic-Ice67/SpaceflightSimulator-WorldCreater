using System;
using System.IO;
using UnityEngine;

namespace PlanetEditor
{
    internal static class Log
    {
        private static string _path;
        private static bool _ready;

        public static void Init(string modFolder)
        {
            try
            {
                if (string.IsNullOrEmpty(modFolder)) modFolder = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Mods", "WorldCreater");
                Directory.CreateDirectory(modFolder);
                _path = Path.Combine(modFolder, "WorldCreater.log");
                File.WriteAllText(_path, "=== WorldCreater " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " ===\n");
                _ready = true;
            }
            catch
            {
                _ready = false;
            }
        }

        public static void Info(string msg)
        {
            Write("INFO  " + msg);
            try { Debug.Log("[WorldCreater] " + msg); } catch { }
        }

        public static void Error(string msg)
        {
            Write("ERROR " + msg);
            try { Debug.LogError("[WorldCreater] " + msg); } catch { }
        }

        private static void Write(string line)
        {
            if (!_ready) return;
            try { File.AppendAllText(_path, DateTime.Now.ToString("HH:mm:ss.fff") + " " + line + "\n"); }
            catch { }
        }
    }
}
