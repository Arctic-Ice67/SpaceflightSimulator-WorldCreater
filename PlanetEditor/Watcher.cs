using System;
using SFS.World;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>挂在一个 DontDestroyOnLoad 物体上，负责每帧推进（热键见 Keys.cs）。</summary>
    public class Watcher : MonoBehaviour
    {
        private void Update()
        {
            try
            {
                WorldCreaterKeys.Tick();
                Bridge.Pump();
                try { LiveApply.EnsureMapPlanets(); } catch (Exception e) { Log.Error("map tick: " + e.Message); }
                EditSession.Tick();
                TickBrushOutsideEdit();
            }
            catch (Exception e)
            {
                Log.Error("watcher update: " + e);
            }
        }

        /// <summary>不在编辑模式也能刷地形（编辑模式里 EditSession.Tick 已经调过一次了）。</summary>
        private static void TickBrushOutsideEdit()
        {
            if (EditSession.Active || !TerrainBrush.Enabled) return;
            Planet p = EditSession.Target;
            if (p == null)
            {
                try
                {
                    if (WorldView.main != null && WorldView.main.ViewLocation != null)
                        p = WorldView.main.ViewLocation.planet;
                }
                catch { }
            }
            if (p != null) TerrainBrush.Tick(p);
        }
    }
}
