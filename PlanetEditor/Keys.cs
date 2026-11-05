using System;
using ModLoader;
using SFS.Input;
using SFS.World;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>模组热键：注册进游戏自带的按键设置页，玩家自己可以改。</summary>
    public class WorldCreaterKeys : ModKeybindings
    {
        public KeybindingsPC.Key ToggleEdit = KeyCode.F8;
        public KeybindingsPC.Key TogglePanel = KeyCode.F11;
        public KeybindingsPC.Key SaveNow = KeyCode.F7;
        public KeybindingsPC.Key ReloadFile = KeyCode.F10;
        public KeybindingsPC.Key ToggleBrush = KeyCode.P;

        private static WorldCreaterKeys _inst;
        private static bool _uiBuilt;
        private static bool _warned;
        private static GameManager _boundGm;

        /// <summary>读键位存档。设置页还没起来时界面往后拖。</summary>
        public static WorldCreaterKeys Load(Mod mod)
        {
            try
            {
                _inst = SetupKeybindings<WorldCreaterKeys>(mod);
            }
            catch (Exception e)
            {
                Log.Error("keybindings load: " + e);
                _inst = new WorldCreaterKeys();
            }
            Log.Info("keybindings loaded, main=" + (KeybindingsPC.main != null));
            return _inst;
        }

        public override void CreateUI()
        {
            if (_uiBuilt) return;
            // 游戏设置页没实例化时 main 是空的，等下一帧再建
            if (KeybindingsPC.main == null)
            {
                if (!_warned)
                {
                    _warned = true;
                    Log.Info("keybindings ui waiting for settings page");
                }
                return;
            }

            CreateUI_Text("World Creater");
            CreateUI_Keybinding(ToggleEdit, KeyCode.F8, "Toggle planet edit");
            CreateUI_Keybinding(TogglePanel, KeyCode.F11, "Toggle editor panel");
            CreateUI_Keybinding(SaveNow, KeyCode.F7, "Save planet");
            CreateUI_Keybinding(ReloadFile, KeyCode.F10, "Reload planet file");
            CreateUI_Keybinding(ToggleBrush, KeyCode.P, "Terrain brush");
            _uiBuilt = true;
            Log.Info("keybindings ui ok");
        }

        /// <summary>每帧由 Watcher 调：补建界面、按场景挂按键。</summary>
        public static void Tick()
        {
            if (_inst == null) return;
            if (!_uiBuilt) _inst.CreateUI();
            BindWorld();
        }

        private static void BindWorld()
        {
            GameManager gm = GameManager.main;
            if (gm == null || gm == _boundGm) return;
            _boundGm = gm;
            try
            {
                GameManager.AddOnKeyDown(Ref(() => _inst.ToggleEdit), () => EditSession.Toggle());
                GameManager.AddOnKeyDown(Ref(() => _inst.TogglePanel), DoTogglePanel);
                GameManager.AddOnKeyDown(Ref(() => _inst.SaveNow), () => EditSession.ApplyNow(true));
                GameManager.AddOnKeyDown(Ref(() => _inst.ReloadFile), () => EditSession.ReloadFromFile());
                GameManager.AddOnKeyDown(Ref(() => _inst.ToggleBrush), DoToggleBrush);
                Log.Info("keybindings bound to world");
            }
            catch (Exception e)
            {
                Log.Error("keybindings bind: " + e);
            }
        }

        private static void DoTogglePanel()
        {
            if (Ui.IsOpen) { Ui.Close(); return; }
            if (EditSession.Target == null)
            {
                Planet[] all = EditSession.AllPlanets();
                if (all.Length > 0) EditSession.Target = all[0];
            }
            if (EditSession.Target != null) Ui.Open();
        }

        private static void DoToggleBrush()
        {
            TerrainBrush.Enabled = !TerrainBrush.Enabled;
            if (TerrainBrush.Enabled) TerrainBrush.Ensure(EditSession.Target);
            Ui.SetStatus(TerrainBrush.Enabled
                ? "PAINT ON  LMB=raise RMB=lower  Ctrl+wheel=brush " + (int)TerrainBrush.RadiusMeters + "m"
                : "PAINT OFF");
        }

        private static I_Key Ref(Func<KeybindingsPC.Key> get)
        {
            return new KeyRef(get);
        }
    }

    /// <summary>包装一层，改键后自动生效；同一帧只报一次按下，免得世界和地图两套输入各触发一遍。</summary>
    internal sealed class KeyRef : I_Key
    {
        private readonly Func<KeybindingsPC.Key> _get;
        private int _frame = -1;

        public KeyRef(Func<KeybindingsPC.Key> get)
        {
            _get = get;
        }

        public bool IsKeyDown()
        {
            KeybindingsPC.Key k = _get();
            if (k == null) return false;
            if (!((I_Key)k).IsKeyDown()) return false;
            if (_frame == Time.frameCount) return false;
            _frame = Time.frameCount;
            return true;
        }

        public bool IsKeyStay()
        {
            KeybindingsPC.Key k = _get();
            return k != null && ((I_Key)k).IsKeyStay();
        }

        public bool IsKeyUp()
        {
            KeybindingsPC.Key k = _get();
            return k != null && ((I_Key)k).IsKeyUp();
        }
    }
}
