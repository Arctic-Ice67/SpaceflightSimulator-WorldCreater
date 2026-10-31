using System;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>挂在一个 DontDestroyOnLoad 物体上，负责热键与每帧推进。</summary>
    public class Watcher : MonoBehaviour
    {
        private void Update()
        {
            try
            {
                if (Input.GetKeyDown(KeyCode.F8))
                {
                    EditSession.Toggle();
                }
                // F9：调试用，不进入编辑模式也能开面板（只要星球已经加载）
                if (Input.GetKeyDown(KeyCode.F11))
                {
                    if (Ui.IsOpen)
                    {
                        Ui.Close();
                    }
                    else
                    {
                        if (EditSession.Target == null)
                        {
                            Planet[] all = EditSession.AllPlanets();
                            if (all.Length > 0) EditSession.Target = all[0];
                        }
                        if (EditSession.Target != null) Ui.Open();
                    }
                }
                Bridge.Pump();
                try { LiveApply.EnsureMapPlanets(); } catch (Exception e) { Log.Error("map tick: " + e.Message); }
                Ui.TickInput();
                EditSession.Tick();
            }
            catch (Exception e)
            {
                Log.Error("watcher update: " + e);
            }
        }
    }
}
