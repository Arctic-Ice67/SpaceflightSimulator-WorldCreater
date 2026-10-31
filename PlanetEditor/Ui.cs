using System;
using System.Collections.Generic;
using ModLoader;
using SFS;
using SFS.UI.ModGUI;
using SFS.WorldBase;
using TMPro;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;
using GuiType = SFS.UI.ModGUI.Type;
using GuiButton = SFS.UI.ModGUI.Button;

namespace PlanetEditor
{
    /// <summary>
    /// 编辑面板。做法参考 UITools 的 ClosableWindow：
    /// 窗口挂在游戏的 UI 层（Builder.CreateHolder + CreateWindow），用标准 ModGUI 控件，
    /// 折叠 = 把窗口高度压成标题栏 + 重建布局；另外补一套键盘操作以防鼠标点不到。
    /// </summary>
    internal static class Ui
    {
        private const int RowsPerPage = 10;

        private const int WinW = 620;
        private const int WinH = 610;
        private const int OffsetX = 340;
        private const int RowH = 30;
        private const int RowGap = 2;
        private const int BarH = 38;
        private const int CollapsedH = 54;

        private static float _u = 2.2f;

        private static GameObject _holder;
        private static Window _window;
        private static Label _planetLabel;
        private static Label _pageLabel;
        private static Label _statusLabel;
        private static GuiButton _autoSaveButton;
        private static GuiButton _collapseButton;
        private static Container _header;
        private static GuiButton _pgMinus;
        private static GuiButton _pgPlus;
        private static Container _toolbar;
        private static readonly List<GameObject> _rows = new List<GameObject>();
        private static readonly List<RowRef> _pageRows = new List<RowRef>();
        private static List<FieldEntry> _entries = new List<FieldEntry>();
        private static int _page;
        private static int _sel;
        private static bool _collapsed;
        private static string _pendingStatus = "ready";

        private static RawImage _preview;
        private static Texture2D _previewTex;
        private static float _lastPreview;
        private static PointerEventData _ped;
        private static readonly List<RaycastResult> _rayResults = new List<RaycastResult>();

        private class RowRef
        {
            public FieldEntry entry;
            public Label label;
            public TextInput input;
        }

        private static int U(float px) { return Mathf.RoundToInt(px * _u); }
        private static float F(float px) { return px * _u; }

        public static bool IsOpen { get { return _holder != null; } }

        public static void Open()
        {
            if (_holder != null)
            {
                Rebuild();
                return;
            }
            _window = null;
            _planetLabel = null;
            _pageLabel = null;
            _statusLabel = null;
            _autoSaveButton = null;
            _collapseButton = null;
            _toolbar = null;
            _rows.Clear();
            _pageRows.Clear();

            try
            {
                // 和 UITools 一样：挂到游戏自己的 UI 层，别自建 Canvas（自建会收不到 uGUI 输入）
                _holder = Builder.CreateHolder(Builder.SceneToAttach.CurrentScene, "PlanetEditor_UI");

                Canvas canvas = _holder.GetComponentInParent<Canvas>();
                float scale = canvas != null ? canvas.scaleFactor : 0f;
                if (scale > 0.01f) _u = 1f / scale;
                Log.Info("UI: screen=" + Screen.width + "x" + Screen.height +
                         " canvasScale=" + scale + " unitsPerPx=" + _u);

                _window = Builder.CreateWindow(_holder.transform, Builder.GetRandomID(), U(WinW), U(WinH), U(360), U(300), true, false, 1f, "WORLD CREATER");
                try
                {
                    _window.WindowColor = new Color(0.06f, 0.08f, 0.12f, 1f);
                    _window.WindowOpacity = 0.94f;
                    _window.TitleOpacity = 1f;
                }
                catch { }

                _window.CreateLayoutGroup(GuiType.Vertical, TextAnchor.UpperCenter, F(3));
                try { _window.EnableScrolling(GuiType.Vertical); } catch (Exception e) { Log.Error("scrolling: " + e.Message); }

                BuildHeader();
                BuildToolbar();
                BuildPreview();
                _statusLabel = Builder.CreateLabel(_window, U(WinW - 16), U(26), 0, 0, _pendingStatus);
                _statusLabel.FontSize = F(11);

                ApplyGeometry();
                Rebuild();
                Log.Info("UI opened");
            }
            catch (Exception e)
            {
                Log.Error("UI open: " + e);
                Close();
            }
        }

        /// <summary>折叠 = 只改窗口高度（照抄 UITools ClosableWindow 的做法）。</summary>
        private static void ApplyGeometry()
        {
            try
            {
                _window.Size = new Vector2(U(WinW), U(_collapsed ? CollapsedH : WinH));
                TryRebuildLayout();
            }
            catch (Exception e)
            {
                Log.Error("geometry: " + e);
            }
        }

        private static void TryRebuildLayout()
        {
            try
            {
                Transform ch = _window.ChildrenHolder;
                if (ch != null) LayoutRebuilder.ForceRebuildLayoutImmediate((RectTransform)ch);
            }
            catch { }
        }

        public static void Close()
        {
            try
            {
                if (_holder != null) UnityEngine.Object.Destroy(_holder);
            }
            catch { }
            _holder = null;
            _window = null;
            _planetLabel = null;
            _pageLabel = null;
            _statusLabel = null;
            _autoSaveButton = null;
            _collapseButton = null;
            _toolbar = null;
            _rows.Clear();
            _pageRows.Clear();
            _entries = new List<FieldEntry>();
            _collapsed = false;
        }

        public static void Rebuild()
        {
            if (_window == null) return;
            _entries = FieldTree.For(EditSession.Target);
            _page = 0;
            _sel = 0;
            Render();
        }

        public static void SetStatus(string text)
        {
            _pendingStatus = text ?? string.Empty;
            try
            {
                if (_statusLabel != null) _statusLabel.Text = _pendingStatus;
            }
            catch { }
        }

        private static void BuildHeader()
        {
            Container bar = Builder.CreateContainer(_window, 0, 0);
            bar.Size = new Vector2(U(WinW - 16), U(BarH));
            bar.CreateLayoutGroup(GuiType.Horizontal, TextAnchor.MiddleCenter, F(6));

            _header = bar;
            _collapseButton = MakeButton(bar, 28, 28, ToggleCollapsed, "▼");
            MakeButton(bar, 28, 28, () => EditSession.NextPlanet(-1), "<");
            _planetLabel = Builder.CreateLabel(bar, U(200), U(28), 0, 0, "-");
            _planetLabel.FontSize = F(18);
            MakeButton(bar, 28, 28, () => EditSession.NextPlanet(1), ">");
            _pgMinus = MakeButton(bar, 58, 28, () => { _page--; Render(); }, "PG-");
            _pageLabel = Builder.CreateLabel(bar, U(100), U(28), 0, 0, "1/1");
            _pageLabel.FontSize = F(13);
            _pgPlus = MakeButton(bar, 58, 28, () => { _page++; Render(); }, "PG+");
        }

        private static void BuildToolbar()
        {
            _toolbar = Builder.CreateContainer(_window, 0, 0);
            _toolbar.Size = new Vector2(U(WinW - 16), U(BarH));
            _toolbar.CreateLayoutGroup(GuiType.Horizontal, TextAnchor.MiddleCenter, F(6));

            MakeButton(_toolbar, 96, 28, () => EditSession.ApplyNow(true), "APPLY + SAVE");
            MakeButton(_toolbar, 64, 28, () => EditSession.ApplyNow(false), "APPLY");
            MakeButton(_toolbar, 88, 28, EditSession.ReloadFromFile, "RELOAD FILE");
            _autoSaveButton = MakeButton(_toolbar, 96, 28, ToggleAutoSave, "AUTOSAVE: ON");
            MakeButton(_toolbar, 88, 28, PackIO.RescanTextures, "RESCAN TEX");
            _toolbar.gameObject.SetActive(!_collapsed);
        }

        private static GuiButton MakeButton(Transform parent, int wPx, int hPx, Action onClick, string text)
        {
            GuiButton b = Builder.CreateButton(parent, U(wPx), U(hPx), 0, 0, onClick, text);
            try
            {
                Transform t = b.gameObject.transform.Find("Text");
                if (t != null)
                {
                    var tmp = t.GetComponent<TextMeshProUGUI>();
                    if (tmp != null) tmp.fontSize = F(12);
                }
            }
            catch { }
            return b;
        }

        private static void ToggleCollapsed()
        {
            _collapsed = !_collapsed;
            if (_collapseButton != null) _collapseButton.Text = _collapsed ? "▲" : "▼";
            if (_toolbar != null) _toolbar.gameObject.SetActive(!_collapsed);
            if (_pgMinus != null) _pgMinus.gameObject.SetActive(!_collapsed);
            if (_pgPlus != null) _pgPlus.gameObject.SetActive(!_collapsed);
            if (_pageLabel != null) _pageLabel.gameObject.SetActive(!_collapsed);
            if (_statusLabel != null) _statusLabel.gameObject.SetActive(!_collapsed);
            for (int i = 0; i < _rows.Count; i++)
            {
                if (_rows[i] != null) _rows[i].SetActive(!_collapsed);
            }
            ApplyGeometry();
            SetStatus(_collapsed ? "collapsed (Tab / + to expand)" : "expanded");
        }

        private static void ToggleAutoSave()
        {
            EditSession.AutoSave = !EditSession.AutoSave;
            if (_autoSaveButton != null)
                _autoSaveButton.Text = EditSession.AutoSave ? "AUTOSAVE: ON" : "AUTOSAVE: OFF";
            SetStatus(EditSession.AutoSave ? "autosave on" : "autosave off (press APPLY + SAVE)");
        }

        private static void Render()
        {
            if (_window == null) return;

            for (int i = 0; i < _rows.Count; i++)
            {
                try { if (_rows[i] != null) UnityEngine.Object.Destroy(_rows[i]); } catch { }
            }
            _rows.Clear();
            _pageRows.Clear();

            int total = _entries != null ? _entries.Count : 0;
            int pages = Mathf.Max(1, (total + RowsPerPage - 1) / RowsPerPage);
            _page = Mathf.Clamp(_page, 0, pages - 1);

            if (_planetLabel != null)
            {
                Planet t = EditSession.Target;
                _planetLabel.Text = t != null ? t.codeName : "-";
            }
            if (_pageLabel != null) _pageLabel.Text = (_page + 1) + "/" + pages + "  (" + total + ")";

            int start = _page * RowsPerPage;
            int end = Mathf.Min(start + RowsPerPage, total);
            for (int i = start; i < end; i++)
            {
                FieldEntry entry = _entries[i];
                if (entry != null) BuildRow(entry);
            }
            if (_sel >= _pageRows.Count) _sel = Mathf.Max(0, _pageRows.Count - 1);
            Highlight();
            TryRebuildLayout();
        }

        private static void BuildRow(FieldEntry entry)
        {
            Container row = Builder.CreateContainer(_window, 0, 0);
            row.Size = new Vector2(U(WinW - 16), U(RowH));
            row.CreateLayoutGroup(GuiType.Horizontal, TextAnchor.MiddleLeft, F(RowGap));
            row.gameObject.SetActive(!_collapsed);
            _rows.Add(row.gameObject);

            bool isBool = entry.Kind == EntryKind.Bool;
            bool isTexture = entry.Kind == EntryKind.Texture;

            int labelW = isBool ? (WinW - 90) : (isTexture ? 340 : 380);
            Label label = Builder.CreateLabel(row, U(labelW), U(RowH - 2), 0, 0, Shorten(entry.Label));
            label.FontSize = F(13);
            label.TextAlignment = TextAlignmentOptions.Left;

            if (isBool)
            {
                Builder.CreateToggle(row, () =>
                {
                    try { return (bool)entry.Get(); } catch { return false; }
                }, 0, 0, () =>
                {
                    try
                    {
                        entry.Set(!(bool)entry.Get());
                        EditSession.MarkDirty();
                        SetStatus(entry.Key + " = " + (!(bool)entry.Get()));
                    }
                    catch (Exception e) { Log.Error("toggle: " + e); }
                });
                _pageRows.Add(new RowRef { entry = entry, label = label, input = null });
                return;
            }

            TextInput input = Builder.CreateTextInput(row, U(170), U(RowH - 2), 0, 0, entry.GetText(), s =>
            {
                if (entry.TrySetText(s))
                {
                    EditSession.MarkDirty();
                    SetStatus("set " + entry.Key + " = " + s);
                }
            });
            try
            {
                if (input.field != null && input.field.textComponent != null)
                {
                    input.field.textComponent.fontSize = F(13);
                    input.field.pointSize = F(13);
                }
            }
            catch { }

            _pageRows.Add(new RowRef { entry = entry, label = label, input = input });

            if (isTexture)
            {
                MakeButton(row, 26, RowH - 2, () => CycleTexture(entry, -1), "<");
                MakeButton(row, 26, RowH - 2, () => CycleTexture(entry, 1), ">");
            }
        }

        private static void Highlight()
        {
            for (int i = 0; i < _pageRows.Count; i++)
            {
                RowRef r = _pageRows[i];
                if (r == null || r.label == null) continue;
                try { r.label.Color = (i == _sel) ? new Color(0.6f, 0.9f, 1f) : Color.white; } catch { }
            }
        }

        /// <summary>键盘用：把选中行聚焦到它的输入框（TMP 自己处理打字）。</summary>
        private static void FocusSelected()
        {
            if (_collapsed || _sel < 0 || _sel >= _pageRows.Count) return;
            RowRef r = _pageRows[_sel];
            if (r == null || r.entry == null) return;
            if (r.entry.Kind == EntryKind.Bool)
            {
                bool v = (bool)r.entry.Get();
                r.entry.Set(!v);
                EditSession.MarkDirty();
                SetStatus(r.entry.Key + " = " + (!v));
                return;
            }
            if (r.input != null && r.input.field != null)
            {
                try
                {
                    r.input.field.interactable = true;
                    r.input.field.ActivateInputField();
                    r.input.field.Select();
                    SetStatus("editing " + r.entry.Key + " (Enter=apply)");
                }
                catch (Exception e) { Log.Error("focus: " + e); }
            }
        }

        /// <summary>每帧由 Watcher 调用：只做键盘导航（鼠标交给 uGUI）。</summary>
        public static void TickInput()
        {
            if (!IsOpen) return;

            if (Input.GetKeyDown(KeyCode.Tab)) { ToggleCollapsed(); return; }
            if (Input.GetKeyDown(KeyCode.F7)) { EditSession.ApplyNow(true); return; }
            if (Input.GetKeyDown(KeyCode.F10)) { EditSession.ReloadFromFile(); return; }

            if (TerrainBrush.Enabled && (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl)) && Input.GetKeyDown(KeyCode.Z))
            {
                bool ok = TerrainBrush.Undo(EditSession.Target);
                SetStatus(ok ? "undo terrain stroke (" + TerrainBrush.UndoCount + " left)" : "nothing to undo");
                return;
            }

            if (Input.GetKeyDown(KeyCode.P))
            {
                TerrainBrush.Enabled = !TerrainBrush.Enabled;
                if (TerrainBrush.Enabled) TerrainBrush.Ensure(EditSession.Target);
                SetStatus(TerrainBrush.Enabled
                    ? "PAINT ON  LMB=raise RMB=lower  Ctrl+wheel=brush " + (int)TerrainBrush.RadiusMeters + "m"
                    : "PAINT OFF");
                return;
            }
            if (TerrainBrush.Enabled && (Input.GetKey(KeyCode.LeftControl) || Input.GetKey(KeyCode.RightControl)))
            {
                float sc = Input.mouseScrollDelta.y;
                if (Mathf.Abs(sc) > 0.001f)
                {
                    TerrainBrush.RadiusMeters = Mathf.Clamp(TerrainBrush.RadiusMeters * Mathf.Pow(1.25f, sc), 10f, 20000f);
                    SetStatus("brush radius " + (int)TerrainBrush.RadiusMeters + " m");
                }
            }
            if (Time.unscaledTime - _lastPreview > 1.5f) RefreshPreview();

            // 翻页键不冲突，输入框聚焦时也允许
            if (Input.GetKeyDown(KeyCode.PageDown)) { _page++; Render(); return; }
            if (Input.GetKeyDown(KeyCode.PageUp)) { _page--; Render(); return; }

            // 有输入框在编辑时，别抢回车/方向键
            bool inputFocused = false;
            try
            {
                UnityEngine.EventSystems.EventSystem es = UnityEngine.EventSystems.EventSystem.current;
                if (es != null && es.currentSelectedGameObject != null)
                    inputFocused = es.currentSelectedGameObject.GetComponent<TMP_InputField>() != null;
            }
            catch { }
            if (inputFocused)
            {
                if (Input.GetKeyDown(KeyCode.Escape))
                {
                    try
                    {
                        UnityEngine.EventSystems.EventSystem es = UnityEngine.EventSystems.EventSystem.current;
                        if (es != null) es.SetSelectedGameObject(null);
                    }
                    catch { }
                    SetStatus("input released");
                }
                return;
            }

            if (_collapsed) return;

            if (Input.GetKeyDown(KeyCode.DownArrow)) { _sel++; if (_sel >= _pageRows.Count) _sel = _pageRows.Count - 1; Highlight(); }
            if (Input.GetKeyDown(KeyCode.UpArrow)) { _sel--; if (_sel < 0) _sel = 0; Highlight(); }
            // Enter 和游戏的 Activate_Stage 冲突，已移除（直接点输入框即可）

        }

        private static void BuildPreview()
        {
            try
            {
                var go = new GameObject("TerrainPreview", typeof(RectTransform), typeof(CanvasRenderer), typeof(RawImage));
                go.transform.SetParent(_window.ChildrenHolder, false);
                RectTransform rt = go.GetComponent<RectTransform>();
                rt.sizeDelta = new Vector2(U(WinW - 16), U(120));
                _preview = go.GetComponent<RawImage>();
                _previewTex = new Texture2D(560, 120, TextureFormat.RGBA32, false);
                _preview.texture = _previewTex;
                _preview.raycastTarget = false;
                RefreshPreview();
            }
            catch (Exception e)
            {
                Log.Error("preview build: " + e);
            }
        }

        /// <summary>画一条"全周地形高度剖面"：横轴 0..2π，纵轴归一化高度。</summary>
        public static void RefreshPreview()
        {
            try
            {
                if (_previewTex == null) return;
                int w = _previewTex.width;
                int h = _previewTex.height;
                var px = new Color32[w * h];
                for (int i = 0; i < px.Length; i++) px[i] = new Color32(10, 16, 24, 255);

                Planet p = EditSession.Target;

                // ---- 左半边：星球真实轮廓（地形高度决定形状，贴图决定颜色）----
                int D = Math.Min(h, w);
                float cx = D * 0.5f, cy = h * 0.5f;
                if (p != null && p.data != null)
                {
                    const int N = 360;
                    var angs = new double[N];
                    for (int i = 0; i < N; i++) angs[i] = Math.PI * 2.0 * i / N;
                    double[] hs;
                    try { hs = p.GetTerrainHeightAtAngles(angs, false); } catch { hs = new double[N]; }
                    if (hs == null || hs.Length < N) hs = new double[N];

                    double R = p.Radius;
                    double maxSurf = R;
                    for (int i = 0; i < N; i++)
                    {
                        double s = R + hs[i];
                        if (s > maxSurf) maxSurf = s;
                    }
                    double scale = (D * 0.5) / (maxSurf * 1.02);

                    Texture2D tex = p.planetTexture;
                    float cut = 1f;
                    try { cut = p.data.terrain.TERRAIN_TEXTURE_DATA.planetTextureCutout; } catch { }
                    if (cut <= 0f) cut = 1f;
                    Color32 fallback = new Color32(120, 120, 120, 255);
                    try { Color mc = p.data.basics.mapColor; fallback = new Color32((byte)(mc.r*255), (byte)(mc.g*255), (byte)(mc.b*255), 255); } catch { }

                    for (int y = 0; y < D; y++)
                    {
                        for (int x = 0; x < D; x++)
                        {
                            float dx = x - cx + 0.5f;
                            float dy = y - cy + 0.5f;
                            float rpx = Mathf.Sqrt(dx * dx + dy * dy);
                            if (rpx > D * 0.5f) continue;
                            double ang = Math.Atan2(dy, dx);
                            if (ang < 0) ang += Math.PI * 2.0;
                            int bi = (int)(ang / (Math.PI * 2.0) * N);
                            if (bi < 0) bi = 0; if (bi >= N) bi = N - 1;
                            double surf = (R + hs[bi]) * scale;
                            bool rim = rpx > surf - 1.6f;
                            if (rpx > surf) continue;
                            Color32 cc = fallback;
                            if (tex != null)
                            {
                                float u = (dx / D) * cut + 0.5f;
                                float v = (dy / D) * cut + 0.5f;
                                try
                                {
                                    Color tc = tex.GetPixelBilinear(u, v);
                                    cc = new Color32((byte)Mathf.Clamp(tc.r * 255f, 0f, 255f),
                                                     (byte)Mathf.Clamp(tc.g * 255f, 0f, 255f),
                                                     (byte)Mathf.Clamp(tc.b * 255f, 0f, 255f), 255);
                                }
                                catch { }
                            }
                            float shade = rim ? 1.25f : (0.9f + 0.1f * (1f - (float)(rpx / Math.Max(1.0, surf))));
                            px[y * w + x] = new Color32(
                                (byte)Mathf.Clamp(cc.r * shade, 0f, 255f),
                                (byte)Mathf.Clamp(cc.g * shade, 0f, 255f),
                                (byte)Mathf.Clamp(cc.b * shade, 0f, 255f), 255);
                        }
                    }
                }

                // ---- 右半边：全周地形剖面 ----
                int x0 = D + 6;
                if (p != null && p.data != null && p.data.hasTerrain)
                {
                    int pw = w - x0;
                    if (pw > 8)
                    {
                        double[] angles = new double[pw];
                        for (int i = 0; i < pw; i++) angles[i] = Math.PI * 2.0 * i / (pw - 1);
                        double[] hs = p.GetTerrainHeightAtAngles(angles, false);
                        double min = double.MaxValue, max = double.MinValue;
                        for (int i = 0; i < hs.Length; i++)
                        {
                            if (hs[i] < min) min = hs[i];
                            if (hs[i] > max) max = hs[i];
                        }
                        if (max - min < 1.0) max = min + 1.0;
                        for (int i = 0; i < pw && i < hs.Length; i++)
                        {
                            int top = (int)((hs[i] - min) / (max - min) * (h - 8));
                            if (top < 0) top = 0;
                            if (top > h - 1) top = h - 1;
                            int x = x0 + i;
                            for (int y = 0; y <= top; y++) px[y * w + x] = new Color32(74, 146, 200, 255);
                            px[x] = new Color32(120, 230, 160, 255);
                        }
                    }
                }

                _previewTex.SetPixels32(px);
                _previewTex.Apply(false);
                _lastPreview = Time.unscaledTime;
            }
            catch (Exception e)
            {
                Log.Error("preview draw: " + e);
            }
        }

        /// <summary>当前是否有 TMP 输入框在收键盘（让 WASD 之类让路）。</summary>
        public static bool IsTyping()
        {
            try
            {
                EventSystem es = EventSystem.current;
                if (es == null || es.currentSelectedGameObject == null) return false;
                return es.currentSelectedGameObject.GetComponent<TMP_InputField>() != null;
            }
            catch { return false; }
        }
        /// <summary>鼠标是否落在我们面板上（用 GraphicRaycaster 反查）。</summary>
        public static bool IsPointerOverPanel(Vector2 screenPos)
        {
            try
            {
                if (_holder == null) return false;
                GraphicRaycaster gr = _holder.GetComponentInParent<GraphicRaycaster>();
                if (gr == null) return false;
                if (_ped == null) _ped = new PointerEventData(EventSystem.current);
                if (_rayResults == null) return false;
                _ped.position = screenPos;
                _rayResults.Clear();
                gr.Raycast(_ped, _rayResults);
                for (int i = 0; i < _rayResults.Count; i++)
                {
                    GameObject go = _rayResults[i].gameObject;
                    if (go != null && go.transform.IsChildOf(_holder.transform)) return true;
                }
            }
            catch { }
            return false;
        }
        private static void CycleTexture(FieldEntry entry, int dir)
        {
            try
            {
                List<string> names = TextureNames();
                if (names.Count == 0) { SetStatus("no textures loaded"); return; }
                string cur = entry.GetText();
                int i = names.IndexOf(cur);
                if (i < 0) i = 0;
                else
                {
                    i = (i + dir) % names.Count;
                    if (i < 0) i += names.Count;
                }
                entry.Set(names[i]);
                EditSession.MarkDirty();
                SetStatus(entry.Key + " = " + names[i]);
                Render();
            }
            catch (Exception e)
            {
                Log.Error("cycle texture: " + e);
            }
        }

        private static List<string> TextureNames()
        {
            var list = new List<string>();
            try
            {
                PlanetLoader loader = Base.planetLoader;
                if (loader != null && loader.textures != null)
                {
                    foreach (string k in loader.textures.Keys) list.Add(k);
                }
            }
            catch (Exception e)
            {
                Log.Error("texture names: " + e.Message);
            }
            list.Sort(StringComparer.OrdinalIgnoreCase);
            return list;
        }

        private static string Shorten(string s)
        {
            if (string.IsNullOrEmpty(s)) return string.Empty;
            return s.Length <= 40 ? s : s.Substring(0, 37) + "...";
        }
    }
}
