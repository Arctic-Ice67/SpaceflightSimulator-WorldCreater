using System;
using ModLoader;
using UnityEngine;

namespace PlanetEditor
{
    public class Main : Mod
    {
        public override string ModNameID => "worldcreater";
        public override string DisplayName => "World Creater";
        public override string Author => "Ice";
        public override string Description =>
            "Live planet editing in the world scene: pick a planet, change every value and texture, see it instantly, and the planet file of the solar system (planet pack) is rewritten automatically. Comes with templates, pack metadata, backups and zip export.";
        public override string ModVersion => "v0.3.0";
        public override string MinimumGameVersionNecessary => "1.6.00.18";

        public static Main Instance;
        public static WorldCreaterKeys Keys;

        // 游戏不会主动调 LoadKeybindings（实测没触发），自己在 Load 里读，
        // 设置页起来后再把条目插进按键列表（见 WorldCreaterKeys.Tick）。

        public override void Load()
        {
            Instance = this;
            Log.Init(ModFolder);
            try { Keys = WorldCreaterKeys.Load(this); }
            catch (Exception e) { Log.Error("keybindings: " + e); }
            Bridge.Start();

            var go = new GameObject("WorldCreater_Watcher");
            UnityEngine.Object.DontDestroyOnLoad(go);
            go.AddComponent<Watcher>();
            Log.Info("loaded " + ModVersion);
        }
    }
}
