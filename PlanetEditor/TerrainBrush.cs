using System;
using System.Collections.Generic;
using System.Globalization;
using SFS;
using SFS.World;
using SFS.World.PlanetModules;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>
    /// 地表高度刷。原理：往该星球的地形公式末尾追加一条
    ///   OUTPUT = AddHeightMap(PlanetEditorBrush.txt, &lt;周长&gt;, 1)
    /// 然后把刷出来的高度写进这条曲线的 points[]（单位：米）。曲线对象是共享引用，
    /// 改完下一帧采样就生效，只需重建地形网格。
    /// </summary>
    internal static class TerrainBrush
    {
        public const string CurveName = "PlanetEditorBrush.txt";   // 与包内文件名一致（加载时字典 key 带扩展名）
        public const int PointCount = 4096;

        public static bool Enabled;
        public static float RadiusMeters = 2000f;     // 笔刷半径（米，沿地表）
        public static float RateMeters = 250f;         // 按住时每秒抬高/压低多少米

        private static Planet _planet;
        private static HeightMap _curve;
        private static double _syncedWidth;      // 公式里已经写进去的周长（变了要重写+重编采样器）
        private static float _lastApply = -1f;
        private static bool _dirty;
        private static bool _wasDown;
        private static readonly List<float[]> _redo = new List<float[]>();

        /// <summary>0=圆滑 1=方头 2=尖峰</summary>
        public static int Shape;
        private static readonly List<float[]> _undo = new List<float[]>();
        private const int UndoMax = 30;

        public static HeightMap Curve { get { return _curve; } }

        /// <summary>公式里当前写着的周长（诊断用：应等于 2π·radius）。</summary>
        public static double FormulaWidth { get { return _syncedWidth; } }

        /// <summary>确保曲线存在、公式里挂着、采样器已按新公式编译。返回 true 表示采样器被重编过（地形需要重建）。</summary>
        public static bool Ensure(Planet planet)
        {
            if (planet == null || planet.data == null || planet.data.terrain == null) return false;
            if (!ReferenceEquals(_planet, planet))
            {
                _planet = planet;
                _curve = null;
                _syncedWidth = 0.0;
            }

            bool replaced = false;
            if (_curve == null) replaced = CreateOrReuseCurve();
            // 关键：公式里那条 AddHeightMap 的"周长"决定角度→采样点的映射。
            // 星球半径改过、或以前写进去的是旧周长，就得整条重写，否则笔刷会偏移（写在这个角度、显示在另一个角度）。
            bool formula = false;
            double circ = planet.data.basics.radius * Math.PI * 2.0;
            if (Math.Abs(_syncedWidth - circ) > 0.5)
            {
                formula = EnsureFormula(planet);
                _syncedWidth = circ;
            }

            if (formula || replaced)
            {
                try
                {
                    planet.data.terrain.SetupSamplers(planet.codeName, new MsgNone());
                    planet.maxTerrainHeight = planet.data.terrain.GetMaxTerrainHeight(planet) + 200.0;
                }
                catch (Exception e)
                {
                    Log.Error("brush: setup samplers: " + e);
                }
                return true;
            }
            return false;
        }

        /// <summary>建立（或复用）笔刷高度图对象，返回 true 表示采样器需要重编。</summary>
        private static bool CreateOrReuseCurve()
        {
            PlanetLoader loader = Base.planetLoader;
            if (loader == null) return false;

            bool replaced = false;
            HeightMap existing;
            if (loader.heightmaps.TryGetValue(CurveName, out existing) && existing != null && existing.points != null)
            {
                if (existing.points.Length == PointCount)
                {
                    _curve = existing;
                    Log.Info("brush: reuse heightmap " + CurveName + " (" + existing.points.Length + " pts)");
                }
                else
                {
                    // 点数不一致就线性重采样到 PointCount（否则笔刷只有两三个点宽，太糙）
                    _curve = new HeightMap(Resample(existing.points, PointCount));
                    loader.heightmaps[CurveName] = _curve;
                    replaced = true;
                    Log.Info("brush: resampled heightmap " + existing.points.Length + " -> " + PointCount + " pts");
                }
            }
            else
            {
                _curve = new HeightMap(new float[PointCount]);
                loader.heightmaps[CurveName] = _curve;
                replaced = true;
                Log.Info("brush: created heightmap " + CurveName);
            }

            return replaced;
        }

        /// <summary>笔刷权重（t = 0..1，0 在中心）。</summary>
        private static float Weight(double t)
        {
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            if (Shape == 1) return 1f;                       // 方头：硬边
            if (Shape == 2) { float u = (float)(1.0 - t); return u * u; }   // 尖峰
            float w = (float)(1.0 - t);
            return w * w * (3f - 2f * w);                    // 圆滑（smoothstep）
        }
        /// <summary>线性重采样（保持形状，点数翻倍时最好用）。</summary>
        private static float[] Resample(float[] src, int dstCount)
        {
            if (src == null || src.Length < 2) return new float[dstCount];
            var dst = new float[dstCount];
            for (int i = 0; i < dstCount; i++)
            {
                double t = (double)i / (dstCount - 1) * (src.Length - 1);
                int i0 = (int)t;
                int i1 = Math.Min(i0 + 1, src.Length - 1);
                double f = t - i0;
                dst[i] = (float)(src[i0] * (1.0 - f) + src[i1] * f);
            }
            return dst;
        }
        /// <summary>
        /// 把笔刷曲线挂进地形公式，并保证那条 AddHeightMap 的周长 = 当前星球周长。
        /// 采样器算的是 num = surfaceArea / width（surfaceArea 就是 data.basics.radius），
        /// 所以 width 必须正好等于 2π·radius，否则角度→采样点会被缩放，笔刷就会"画在这里、长在那里"。
        /// 返回 true 表示公式被改动过（需要重编采样器）。
        /// </summary>
        private static bool EnsureFormula(Planet planet)
        {
            TerrainModule t = planet.data.terrain;
            double circ = planet.data.basics.radius * Math.PI * 2.0;
            string line = "OUTPUT = AddHeightMap(" + CurveName + ", " +
                          circ.ToString("R", CultureInfo.InvariantCulture) + ", 1)";
            bool changed = false;

            if (t.terrainFormulaDifficulties != null && t.terrainFormulaDifficulties.Count > 0)
            {
                List<Difficulty.DifficultyType> keys = new List<Difficulty.DifficultyType>(t.terrainFormulaDifficulties.Keys);
                for (int i = 0; i < keys.Count; i++)
                {
                    string[] arr = t.terrainFormulaDifficulties[keys[i]];
                    string[] fixedArr = FixArray(arr, line, ref changed);
                    if (fixedArr != null) t.terrainFormulaDifficulties[keys[i]] = fixedArr;
                }
            }
            else
            {
                string[] fixedArr = FixArray(t.terrainFormula, line, ref changed);
                if (fixedArr != null) t.terrainFormula = fixedArr;
            }

            if (changed)
            {
                Log.Info("brush: formula synced, width=" + circ.ToString("F1", CultureInfo.InvariantCulture)
                         + " (radius=" + planet.data.basics.radius.ToString("F1", CultureInfo.InvariantCulture) + ")");
            }
            return changed;
        }

        /// <summary>存在就校正周长（重复的只留一条），不存在就追加。没有改动时返回 null。</summary>
        private static string[] FixArray(string[] arr, string line, ref bool changed)
        {
            if (arr == null) arr = new string[0];

            List<string> keep = new List<string>(arr.Length + 1);
            bool seen = false, differs = false;
            for (int j = 0; j < arr.Length; j++)
            {
                string s = arr[j];
                if (s != null && s.IndexOf(CurveName, StringComparison.Ordinal) >= 0)
                {
                    if (seen) { differs = true; continue; }   // 重复的丢掉
                    seen = true;
                    if (!string.Equals(s, line, StringComparison.Ordinal)) differs = true;
                    keep.Add(line);                            // 位置不变，内容按摩当前周长重写
                    continue;
                }
                keep.Add(s);
            }
            if (!seen)
            {
                keep.Add(line);
                differs = true;
            }
            if (!differs) return null;
            changed = true;
            return keep.ToArray();
        }

        /// <summary>在某个角度刷一下（deltaMeters &lt; 0 就是挖）。</summary>
        public static void Paint(Planet planet, double angleRadians, float deltaMeters)
        {
            Ensure(planet);
            if (_curve == null || _curve.points == null) return;
            int n = _curve.points.Length;
            if (n < 4) return;

            double halfAngle = Math.Max(1e-6, RadiusMeters / Math.Max(1.0, planet.Radius));
            if (halfAngle > Math.PI) halfAngle = Math.PI;

            for (int i = 0; i < n; i++)
            {
                double a = Math.PI * 2.0 * i / (n - 1);
                double d = a - angleRadians;
                while (d > Math.PI) d -= Math.PI * 2.0;
                while (d < -Math.PI) d += Math.PI * 2.0;
                double ad = Math.Abs(d);
                if (ad > halfAngle) continue;
                float w = Weight(ad / halfAngle);
                _curve.points[i] += deltaMeters * w;
            }
            _dirty = true;
        }

        /// <summary>每帧调用：处理鼠标按住刷 + 节流重建地形。</summary>
        public static void Tick(Planet planet)
        {
            if (!Enabled || planet == null) { _wasDown = false; return; }

            bool downL = Input.GetMouseButton(0);
            bool downR = Input.GetMouseButton(1);
            bool down = downL || downR;

            if (down && !_wasDown && _curve != null && _curve.points != null)
            {
                // 一笔开始前拍快照（撤销用）
                var snap = new float[_curve.points.Length];
                Array.Copy(_curve.points, snap, snap.Length);
                _undo.Add(snap);
                while (_undo.Count > UndoMax) _undo.RemoveAt(0);
            }

            if (down)
            {
                Ensure(planet);
                Double2 p;
                if (TryGetPlanetPoint(out p))
                {
                    // 地形只跟角度有关：只要点在这颗星球圆盘上（或紧贴圆盘上方的天空），
                    // 就按这个方向的地表来刷。以前要求离地表很近，点在圆盘中间根本不生效。
                    double r = p.magnitude;
                    double rim = planet.Radius + Math.Max(planet.maxTerrainHeight, 1000.0);
                    if (r < rim * 1.2)
                    {
                        float delta = RateMeters * Time.unscaledDeltaTime * (downR ? -1f : 1f);
                        Paint(planet, p.AngleRadians, delta);
                    }
                }
            }

            bool justReleased = _wasDown && !down;
            _wasDown = down;

            if (_dirty && (Time.unscaledTime - _lastApply > 0.35f || justReleased))
            {
                _lastApply = Time.unscaledTime;
                _dirty = false;
                try
                {
                    LiveApply.ApplyTerrainOnly(planet);
                }
                catch (Exception e)
                {
                    Log.Error("brush apply: " + e);
                }
            }
        }

        /// <summary>屏幕鼠标 → 星球局部坐标（相机往 z=0 平面投影）。</summary>
        private static bool TryGetPlanetPoint(out Double2 planetLocal)
        {
            planetLocal = default(Double2);
            try
            {
                Camera cam = WorldView.main.worldCamera.camera;
                if (cam == null) return false;
                float z = -cam.transform.position.z;
                Vector3 w = cam.ScreenToWorldPoint(new Vector3(Input.mousePosition.x, Input.mousePosition.y, z));
                planetLocal = (Double2)new Vector2(w.x, w.y) + WorldView.main.positionOffset.Value;
                return true;
            }
            catch (Exception e)
            {
                Log.Error("brush point: " + e);
                return false;
            }
        }

        /// <summary>把笔刷范围内的高度朝目标值收敛（面板里画高度图用）。</summary>
        public static void PaintTo(Planet planet, double angleRadians, double targetHeight, float factor)
        {
            Ensure(planet);
            if (_curve == null || _curve.points == null) return;
            int n = _curve.points.Length;
            if (n < 4) return;
            double halfAngle = Math.Max(1e-6, RadiusMeters / Math.Max(1.0, planet.Radius));
            if (halfAngle > Math.PI) halfAngle = Math.PI;

            for (int i = 0; i < n; i++)
            {
                double a = Math.PI * 2.0 * i / (n - 1);
                double d = a - angleRadians;
                while (d > Math.PI) d -= Math.PI * 2.0;
                while (d < -Math.PI) d += Math.PI * 2.0;
                double ad = Math.Abs(d);
                if (ad > halfAngle) continue;
                float w = Weight(ad / halfAngle);
                double cur = _curve.points[i];
                _curve.points[i] = (float)(cur + (targetHeight - cur) * w * factor);
            }
            _dirty = true;
        }

        /// <summary>画布用：立刻应用并返回（不走 Tick 的节流）。</summary>
        public static void ApplyNow(Planet planet)
        {
            try
            {
                _dirty = false;
                LiveApply.ApplyTerrainOnly(planet);
            }
            catch (Exception e) { Log.Error("brush applynow: " + e); }
        }
        /// <summary>改笔刷曲线的采样点数（会线性重采样，形状不变）。</summary>
        public static bool SetSampleCount(Planet planet, int n)
        {
            try
            {
                Ensure(planet);
                if (_curve == null || _curve.points == null) return false;
                n = Mathf.Clamp(n, 256, 16384);
                if (_curve.points.Length == n) return true;
                int old = _curve.points.Length;
                _curve.points = Resample(_curve.points, n);
                Log.Info("brush: samples " + old + " -> " + n);
                return true;
            }
            catch (Exception e)
            {
                Log.Error("brush samples: " + e);
                return false;
            }
        }

        public static int SampleCount
        {
            get { return _curve != null && _curve.points != null ? _curve.points.Length : 0; }
        }
        /// <summary>撤销上一笔（Ctrl+Z）。</summary>
        public static bool Undo(Planet planet)
        {
            try
            {
                if (_undo.Count == 0) return false;
                Ensure(planet);
                if (_curve == null || _curve.points == null) return false;
                float[] snap = _undo[_undo.Count - 1];
                _undo.RemoveAt(_undo.Count - 1);
                {
                    var curState = new float[_curve.points.Length];
                    Array.Copy(_curve.points, curState, curState.Length);
                    _redo.Add(curState);
                    while (_redo.Count > UndoMax) _redo.RemoveAt(0);
                }
                if (snap.Length == _curve.points.Length)
                    Array.Copy(snap, _curve.points, snap.Length);
                LiveApply.ApplyTerrainOnly(planet);
                Log.Info("brush: undo (" + _undo.Count + " left)");
                return true;
            }
            catch (Exception e)
            {
                Log.Error("brush undo: " + e);
                return false;
            }
        }

        /// <summary>清除笔刷层：曲线全部归零 = 回到没有编辑过的原始地形（可撤销）。</summary>
        public static bool ClearAll(Planet planet)
        {
            try
            {
                Ensure(planet);
                if (_curve == null || _curve.points == null) return false;
                var snap = new float[_curve.points.Length];
                Array.Copy(_curve.points, snap, snap.Length);
                _undo.Add(snap);
                while (_undo.Count > UndoMax) _undo.RemoveAt(0);
                _redo.Clear();
                Array.Clear(_curve.points, 0, _curve.points.Length);
                ApplyNow(planet);
                Log.Info("brush: cleared (back to original terrain)");
                return true;
            }
            catch (System.Exception e)
            {
                Log.Error("brush clear: " + e);
                return false;
            }
        }
        private static float _lastPaintTime = -999f;

        /// <summary>画布落笔：距上一次绘制超过 0.5 秒算新的一笔，先存快照（撤销用）。</summary>
        public static void MaybeBeginStroke()
        {
            float now = Time.unscaledTime;
            if (_lastPaintTime < 0f || now - _lastPaintTime > 1.2f)   // 阈值放宽，避免地形重建卡顿时把一笔拆成两笔
            {
                if (_curve != null && _curve.points != null)
                {
                    var snap = new float[_curve.points.Length];
                    Array.Copy(_curve.points, snap, snap.Length);
                    _undo.Add(snap);
                    while (_undo.Count > UndoMax) _undo.RemoveAt(0);
                    _redo.Clear();
                    Log.Info("brush: stroke snapshot (undo=" + _undo.Count + ")");
                }
            }
            _lastPaintTime = now;
        }
        /// <summary>重做（Ctrl+Y）。</summary>
        public static bool Redo(Planet planet)
        {
            try
            {
                if (_redo.Count == 0) return false;
                Ensure(planet);
                if (_curve == null || _curve.points == null) return false;
                float[] snap = _redo[_redo.Count - 1];
                _redo.RemoveAt(_redo.Count - 1);
                var cur = new float[_curve.points.Length];
                Array.Copy(_curve.points, cur, cur.Length);
                _undo.Add(cur);
                if (snap.Length == _curve.points.Length) Array.Copy(snap, _curve.points, snap.Length);
                LiveApply.ApplyTerrainOnly(planet);
                Log.Info("brush: redo");
                return true;
            }
            catch (Exception e) { Log.Error("brush redo: " + e); return false; }
        }

        public static int RedoCount { get { return _redo.Count; } }
        public static int UndoCount { get { return _undo.Count; } }
        public static void Reset()
        {
            _planet = null;
            _curve = null;
            _dirty = false;
            _wasDown = false;
        }
    }
}
