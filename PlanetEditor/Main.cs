using System;
using HarmonyLib;
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
            "Live planet editing in the world scene: pick a planet, change every value and texture, see it instantly, and the planet file of the solar system (planet pack) is rewritten automatically.";
        public override string ModVersion => "v0.1.0";
        public override string MinimumGameVersionNecessary => "1.6.00.18";

        public static Main Instance;

        public override void Load()
        {
            Instance = this;
            Log.Init(ModFolder);
            try
            {
                var harmony = new Harmony("com.arcticice.worldcreater");
                harmony.PatchAll(typeof(Main).Assembly);
                Log.Info("harmony patchAll ok");
            }
            catch (Exception e)
            {
                Log.Error("harmony: " + e);
            }

            Bridge.Start();

            var go = new GameObject("WorldCreater_Watcher");
            UnityEngine.Object.DontDestroyOnLoad(go);
            go.AddComponent<Watcher>();
            Log.Info("loaded " + ModVersion);
        }
    }
}
