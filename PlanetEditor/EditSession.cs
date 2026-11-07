using System;
using SFS;
using SFS.UI;
using SFS.World;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>编辑模式：上帝视角 + 时间加速锁 + 目标星球。</summary>
    internal static class EditSession
    {
        public static bool Active;
        public static Planet Target;

        public static bool AutoSave = true;

        private static Player _savedPlayer;
        private static Double2 _camPos;
        private static float _camDist = 3000f;
        private static Vector3 _lastMouse;
        private static bool _dragging;
        private static float _dirtyAt = -1f;

        public static void Toggle()
        {
            if (Active) Exit();
            else Enter();
        }

        public static void Enter()
        {
            if (Active) return;
            if (Base.worldBase == null || Base.worldBase.settings == null || GameManager.main == null || WorldView.main == null)
            {
                Report("Enter a world first.");
                return;
            }

            Planet cur = WorldView.main.ViewLocation != null ? WorldView.main.ViewLocation.planet : null;
            if (cur == null) cur = FirstPlanet();
            if (cur == null)
            {
                Report("No planet loaded.");
                return;
            }

            Target = cur;
            _camPos = WorldView.main.ViewLocation.position;
            _camDist = WorldView.main.viewDistance.Value;
            if (_camDist < 12f) _camDist = 3000f;

            _savedPlayer = PlayerController.main != null ? PlayerController.main.player.Value : null;
            try
            {
                if (PlayerController.main != null) PlayerController.main.player.Value = null;
            }
            catch (Exception e)
            {
                Log.Error("detach player: " + e);
            }

            Active = true;
            LockTimewarp();
            Log.Info("edit ON target=" + Target.codeName);
            Report("World Creater: ON   (F8 = exit)");
        }

        public static void Exit()
        {
            if (!Active) return;
            Active = false;
            LockTimewarp();
            try
            {
                if (_savedPlayer != null && PlayerController.main != null)
                    PlayerController.main.player.Value = _savedPlayer;
            }
            catch (Exception e)
            {
                Log.Error("restore player: " + e);
            }
            _savedPlayer = null;
            Log.Info("edit OFF");
            Report("World Creater: OFF");
        }

        /// <summary>外部面板选星球：必要时自动进编辑模式，然后把镜头切过去。</summary>
        public static void SelectPlanet(Planet p)
        {
            if (p == null) return;
            try
            {
                if (!Active)
                {
                    Enter();
                }
                if (!Active) return;
                SetTarget(p);
                LockTimewarp();
                Log.Info("select " + p.codeName + " (edit=" + Active + ")");
            }
            catch (Exception e)
            {
                Log.Error("select planet: " + e);
            }
        }
        public static void SetTarget(Planet p)
        {
            if (p == null) return;
            Target = p;
            try
            {
                // 选中就校正一次笔刷公式的周长（老存档里可能写的是旧周长，会导致笔刷偏移）；
                // 周长变了要重建地形，否则画面上还是旧映射的位置。
                if (TerrainBrush.Ensure(p)) LiveApply.ApplyTerrainOnly(p);
            }
            catch (Exception e) { Log.Error("brush sync: " + e); }
            try
            {
                _camPos = Double2.CosSin(Math.PI / 2.0, p.Radius + Math.Max(p.data.basics.timewarpHeight, 500.0));
                _camDist = (float)Math.Max(p.Radius * 0.08, 500.0);
            }
            catch { }
        }

        public static void NextPlanet(int dir)
        {
            var all = AllPlanets();
            if (all.Length == 0) return;
            int i = Array.IndexOf(all, Target);
            i = (i + dir) % all.Length;
            if (i < 0) i += all.Length;
            SetTarget(all[i]);
        }

        public static Planet[] AllPlanets()
        {
            Planet[] arr = new Planet[0];
            try
            {
                if (Base.planetLoader != null && Base.planetLoader.planets != null && Base.planetLoader.planets.Count > 0)
                {
                    arr = new Planet[Base.planetLoader.planets.Count];
                    Base.planetLoader.planets.Values.CopyTo(arr, 0);
                    Array.Sort(arr, (a, b) => string.Compare(
                        a != null ? a.codeName : "",
                        b != null ? b.codeName : "",
                        StringComparison.OrdinalIgnoreCase));
                }
            }
            catch (Exception e)
            {
                Log.Error("list planets: " + e);
            }
            return arr;
        }

        public static void MarkDirty()
        {
            _dirtyAt = Time.unscaledTime;
        }

        public static void ApplyNow(bool save)
        {
            _dirtyAt = -1f;
            if (Target == null) return;
            bool ok = true;
            try
            {
                LiveApply.Apply(Target);
            }
            catch (Exception e)
            {
                ok = false;
                Log.Error("apply: " + e);
            }
            if (save) SaveNow();
            else Log.Info(ok ? "apply ok" : "apply failed");
        }

        public static void SaveNow()
        {
            if (Target == null) return;
            try
            {
                string path = PackIO.Save(Target);
                Log.Info(path == null ? "saved (no file)" : "saved: " + path);
            }
            catch (Exception e)
            {
                Log.Error("save: " + e);
            }
        }

        public static void ReloadFromFile()
        {
            if (Target == null) return;
            try
            {
                bool ok = PackIO.ReloadPlanet(Target);
                FieldTree.Invalidate();
                LiveApply.Apply(Target);
                Log.Info(ok ? "reloaded from file" : "no file for this planet");
            }
            catch (Exception e)
            {
                Log.Error("reload: " + e);
            }
        }

        public static void Tick()
        {
            if (!Active) return;
            if (Base.worldBase == null || Base.worldBase.settings == null || GameManager.main == null || WorldView.main == null)
            {
                Exit();
                return;
            }

            LockTimewarp();
            try { TerrainBrush.Tick(Target); } catch (Exception e) { Log.Error("brush tick: " + e); }
            HandleInput();
            ApplyCamera();

            if (_dirtyAt > 0f && Time.unscaledTime - _dirtyAt > 0.35f)
            {
                _dirtyAt = -1f;
                try
                {
                    LiveApply.Apply(Target);
                    if (AutoSave) PackIO.Save(Target);
                    Log.Info(AutoSave ? "applied + saved" : "applied");
                }
                catch (Exception e)
                {
                    Log.Error("live apply: " + e);
                }
            }
        }

        private static void LockTimewarp()
        {
            try
            {
                if (WorldTime.main != null && WorldTime.main.timewarpIndex != 0)
                    WorldTime.main.SetTimewarpIndex_ForLoad(0);
            }
            catch (Exception e)
            {
                Log.Error("timewarp lock: " + e);
            }
        }

        private static void HandleInput()
        {
            // 缩放交给游戏自己（滚轮 -> PlayerController.cameraDistance -> viewDistance）。
            // 这边只读回来算拖拽比例，不再自己改，否则两边每帧各写一次，画面就会抽。
            try { _camDist = WorldView.main.viewDistance.Value; } catch { }

            Camera cam = null;
            try { cam = WorldView.main.worldCamera != null ? WorldView.main.worldCamera.camera : null; } catch { }

            bool down = Input.GetMouseButton(1) || Input.GetMouseButton(2);
            Vector3 m = Input.mousePosition;
            if (down && cam != null)
            {
                if (_dragging)
                {
                    Vector3 a = cam.ScreenToWorldPoint(new Vector3(_lastMouse.x, _lastMouse.y, _camDist));
                    Vector3 b = cam.ScreenToWorldPoint(new Vector3(m.x, m.y, _camDist));
                    _camPos -= (Double2)(Vector2)(b - a);
                }
                _dragging = true;
                _lastMouse = m;
            }
            else
            {
                _dragging = false;
            }

            float dx = 0f, dy = 0f;
            // 面板已经搬到外部 exe，游戏里没有输入框了，WASD 一直归镜头
            bool typing = false;
            if (!typing)
            {
                if (Input.GetKey(KeyCode.A)) dx -= 1f;
                if (Input.GetKey(KeyCode.D)) dx += 1f;
                if (Input.GetKey(KeyCode.S)) dy -= 1f;
                if (Input.GetKey(KeyCode.W)) dy += 1f;
                if (Input.GetKey(KeyCode.LeftArrow)) dx -= 1f;
                if (Input.GetKey(KeyCode.RightArrow)) dx += 1f;
                if (Input.GetKey(KeyCode.DownArrow)) dy -= 1f;
                if (Input.GetKey(KeyCode.UpArrow)) dy += 1f;
            }
            if (dx != 0f || dy != 0f)
            {
                float boost = (Input.GetKey(KeyCode.LeftShift) || Input.GetKey(KeyCode.RightShift)) ? 3.5f : 1f;
                float speed = _camDist * 0.9f * Time.unscaledDeltaTime * boost;
                Vector2 d = new Vector2(dx, dy) * speed;
                float rot = 0f;
                try { if (WorldView.main.worldCamera != null) rot = WorldView.main.worldCamera.CameraRotationRadians; } catch { }
                d = d.Rotate_Radians(rot);
                _camPos += (Double2)d;
            }

            double min = Math.Max(1.0, Target.Radius * 0.02);
            double max = Target.SOI;
            if (double.IsNaN(max) || double.IsInfinity(max) || max <= 0.0) max = Target.Radius * 500.0;
            double mag = _camPos.magnitude;
            if (mag < min) _camPos = mag < 1e-6 ? Double2.CosSin(Math.PI / 2.0, min) : _camPos.normalized * min;
            else if (mag > max) _camPos = _camPos.normalized * max;
        }

        private static void ApplyCamera()
        {
            try
            {
                if (Target == null) return;
                WorldView.main.SetViewLocation(new Location(Target, _camPos));
            }
            catch (Exception e)
            {
                Log.Error("camera: " + e);
            }
        }

        private static Planet FirstPlanet()
        {
            Planet[] all = AllPlanets();
            return all.Length > 0 ? all[0] : null;
        }

        public static void Report(string msg)
        {
            try { if (MsgDrawer.main != null) MsgDrawer.main.Log(msg); } catch { }
            Log.Info("msg: " + msg);
        }
    }
}
