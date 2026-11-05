using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using Newtonsoft.Json;
using SFS.World.PlanetModules;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    internal enum EntryKind
    {
        Number,
        Integer,
        Bool,
        Text,
        Texture,
        Formulas        // 多行公式（terrainFormula / textureFormula）
    }

    /// <summary>一个可编辑的叶子字段（反射 + 闭包读写）。</summary>
    internal class FieldEntry
    {
        public string Key;
        public string Label;
        public EntryKind Kind;
        public Type ValueType;
        public Func<object> Get;
        public Action<object> Set;

        public string GetText()
        {
            try
            {
                object v = Get();
                if (v == null) return string.Empty;
                double d;
                if (v is double)
                {
                    d = (double)v;
                    return d.ToString("G10", CultureInfo.InvariantCulture);
                }
                if (v is float)
                {
                    return ((float)v).ToString("G8", CultureInfo.InvariantCulture);
                }
                return Convert.ToString(v, CultureInfo.InvariantCulture);
            }
            catch (Exception e)
            {
                Log.Error("get " + Key + ": " + e.Message);
                return string.Empty;
            }
        }

        public bool TrySetText(string text)
        {
            try
            {
                if (ValueType == typeof(string))
                {
                    Set(text ?? string.Empty);
                    return true;
                }
                if (ValueType == typeof(bool))
                {
                    bool b;
                    if (!bool.TryParse((text ?? string.Empty).Trim(), out b)) return false;
                    Set(b);
                    return true;
                }
                double d;
                if (!double.TryParse((text ?? string.Empty).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out d))
                    return false;
                object v;
                if (ValueType == typeof(int)) v = (int)Math.Round(d);
                else if (ValueType == typeof(long)) v = (long)Math.Round(d);
                else if (ValueType == typeof(float)) v = (float)d;
                else v = d;
                Set(v);
                return true;
            }
            catch (Exception e)
            {
                Log.Error("set " + Key + ": " + e.Message);
                return false;
            }
        }
    }

    /// <summary>把 PlanetData 整棵树摊平成可编辑字段列表。</summary>
    internal static class FieldTree
    {
        /// <summary>数组展开上限，避免超大数组把界面卡死。</summary>
        public const int MaxArrayItems = 512;
        public const int MaxObjectArrayItems = 64;
        public const int MaxDepth = 8;

        private static List<FieldEntry> _cache;
        private static Planet _cacheFor;
        private static readonly HashSet<object> Visited = new HashSet<object>();
        private static int _truncated;

        public static List<FieldEntry> For(Planet planet)
        {
            if (_cache != null && _cacheFor == planet) return _cache;
            var list = new List<FieldEntry>();
            Visited.Clear();
            _truncated = 0;
            try
            {
                PlanetData d = planet.data;
                Walk(list, d.basics, "basics", 0);
                Walk(list, d.atmospherePhysics, "atmo", 0);
                Walk(list, d.atmosphereVisuals, "atmoVis", 0);
                Walk(list, d.terrain, "terrain", 0);
                Walk(list, d.water, "water", 0);
                Walk(list, d.rings, "rings", 0);
                Walk(list, d.frontClouds, "clouds", 0);
                Walk(list, d.postProcessing, "post", 0);
                Walk(list, d.orbit, "orbit", 0);
                WalkLandmarks(list, d.landmarks);
            }
            catch (Exception e)
            {
                Log.Error("build field tree: " + e);
            }
            Log.Info("field tree for " + planet.codeName + ": " + list.Count + " entries"
                + (_truncated > 0 ? (" (truncated " + _truncated + ")") : ""));
            AddTerrainFormulas(list, planet.data);
            _cache = list;
            _cacheFor = planet;
            return list;
        }

        /// <summary>把地图地标也摊平，界面上就能改名字和角度。</summary>
        private static void WalkLandmarks(List<FieldEntry> list, System.Collections.IList landmarks)
        {
            if (landmarks == null) return;
            for (int i = 0; i < landmarks.Count; i++)
            {
                object el = landmarks[i];
                if (el == null) continue;
                Walk(list, el, "landmarks[" + i + "]", 1);
            }
        }

        /// <summary>按前缀过滤字段，给界面做分组显示用。</summary>
        public static List<FieldEntry> Filter(List<FieldEntry> all, string prefix)
        {
            if (string.IsNullOrEmpty(prefix)) return all;
            var list = new List<FieldEntry>();
            for (int i = 0; i < all.Count; i++)
            {
                if (all[i].Key.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) list.Add(all[i]);
            }
            return list;
        }

        public static void Invalidate()
        {
            _cache = null;
            _cacheFor = null;
        }

        private static void Walk(List<FieldEntry> list, object holder, string prefix, int depth)
        {
            if (holder == null || depth > MaxDepth) return;
            if (!holder.GetType().IsValueType)
            {
                if (!Visited.Add(holder)) return;
            }

            foreach (FieldInfo f in holder.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (f.GetCustomAttributes(typeof(JsonIgnoreAttribute), true).Length > 0) continue;
                Type ft = f.FieldType;
                string key = prefix + "." + f.Name;

                if (ft == typeof(double) || ft == typeof(float))
                {
                    AddScalar(list, holder, f, key, EntryKind.Number, ft);
                }
                else if (ft == typeof(int) || ft == typeof(long))
                {
                    AddScalar(list, holder, f, key, EntryKind.Integer, ft);
                }
                else if (ft == typeof(bool))
                {
                    AddScalar(list, holder, f, key, EntryKind.Bool, ft);
                }
                else if (ft == typeof(string))
                {
                    EntryKind kind = f.Name.ToLowerInvariant().Contains("texture") ? EntryKind.Texture : EntryKind.Text;
                    AddScalar(list, holder, f, key, kind, ft);
                }
                else if (ft == typeof(Vector2))
                {
                    AddStruct(list, holder, f, key, "x");
                    AddStruct(list, holder, f, key, "y");
                }
                else if (ft == typeof(Color))
                {
                    AddStruct(list, holder, f, key, "r");
                    AddStruct(list, holder, f, key, "g");
                    AddStruct(list, holder, f, key, "b");
                    AddStruct(list, holder, f, key, "a");
                }
                else if (ft.IsEnum)
                {
                    AddScalar(list, holder, f, key, EntryKind.Text, ft);
                }
                else if (ft.IsArray)
                {
                    Type et = ft.GetElementType();
                    if (et == typeof(string) || et == typeof(double) || et == typeof(float) || et == typeof(int))
                    {
                        var arr = f.GetValue(holder) as Array;
                        if (arr == null) continue;
                        int n = Math.Min(arr.Length, MaxArrayItems);
                        if (arr.Length > n) _truncated += arr.Length - n;
                        for (int i = 0; i < n; i++)
                        {
                            int idx = i;
                            Array target = arr;
                            EntryKind kind = et == typeof(string) ? EntryKind.Text : EntryKind.Number;
                            list.Add(new FieldEntry
                            {
                                Key = key + "[" + i + "]",
                                Label = key + "[" + i + "]",
                                Kind = kind,
                                ValueType = et,
                                Get = () => target.GetValue(idx),
                                Set = v => target.SetValue(v, idx)
                            });
                        }
                    }
                    else if (et != null && et.IsClass && et.Namespace != null && et.Namespace.StartsWith("SFS", StringComparison.Ordinal))
                    {
                        // 对象数组（FOG 关键帧 / PostProcessing 关键帧 / flatZones / landmarks）
                        // 逐个元素展开成普通字段行，改的是数组里的同一个实例
                        var arr = f.GetValue(holder) as Array;
                        if (arr == null) continue;
                        int n = Math.Min(arr.Length, MaxObjectArrayItems);
                        if (arr.Length > n) _truncated += arr.Length - n;
                        for (int i = 0; i < n; i++)
                        {
                            object el = arr.GetValue(i);
                            if (el == null) continue;
                            Walk(list, el, key + "[" + i + "]", depth + 1);
                        }
                    }
                }
                else if (ft.IsGenericType && ft.GetGenericTypeDefinition() == typeof(List<>) )
                {
                    Type et = ft.GetGenericArguments()[0];
                    if (et.IsClass && et.Namespace != null && et.Namespace.StartsWith("SFS", StringComparison.Ordinal))
                    {
                        var lst = f.GetValue(holder) as System.Collections.IList;
                        if (lst == null) continue;
                        int n = Math.Min(lst.Count, MaxObjectArrayItems);
                        if (lst.Count > n) _truncated += lst.Count - n;
                        for (int i = 0; i < n; i++)
                        {
                            object el = lst[i];
                            if (el == null) continue;
                            Walk(list, el, key + "[" + i + "]", depth + 1);
                        }
                    }
                }
                else if (ft.IsClass && ft.Namespace != null && ft.Namespace.StartsWith("SFS", StringComparison.Ordinal))
                {
                    Walk(list, f.GetValue(holder), key, depth + 1);
                }
            }
        }

        /// <summary>多行公式条目：传输时行间用两个字面字符 \n 分隔，避免破坏按行协议。</summary>
        private static void AddFormula(List<FieldEntry> list, string key, Func<string[]> get, Action<string[]> set)
        {
            list.Add(new FieldEntry
            {
                Key = key,
                Label = key,
                Kind = EntryKind.Formulas,
                ValueType = typeof(string),   // 原样把文本交给 Set，由闭包自己拆行
                Get = () =>
                {
                    string[] a = get();
                    return a == null ? "" : string.Join("\\n", a);
                },
                Set = (object v) =>
                {
                    string s = v == null ? "" : v.ToString();
                    set(string.IsNullOrEmpty(s) ? new string[0] : s.Split(new[] { "\\n" }, StringSplitOptions.None));
                }
            });
        }

        /// <summary>把地形/贴图公式（字典 + 数组）加进字段树。</summary>
        private static void AddTerrainFormulas(List<FieldEntry> list, PlanetData d)
        {
            if (d == null || d.terrain == null) return;
            TerrainModule t = d.terrain;

            AddFormula(list, "terrain.formula.base",
                () => t.terrainFormula,
                v => t.terrainFormula = v);

            if (t.terrainFormulaDifficulties != null)
            {
                foreach (var kv in new System.Collections.Generic.List<KeyValuePair<Difficulty.DifficultyType, string[]>>(t.terrainFormulaDifficulties))
                {
                    var diff = kv.Key;
                    AddFormula(list, "terrain.formula." + diff.ToString(),
                        () => t.terrainFormulaDifficulties.ContainsKey(diff) ? t.terrainFormulaDifficulties[diff] : null,
                        v => t.terrainFormulaDifficulties[diff] = v);
                }
            }

            AddFormula(list, "terrain.textureFormula",
                () => t.textureFormula,
                v => t.textureFormula = v);
        }
        private static void AddScalar(List<FieldEntry> list, object holder, FieldInfo f, string key, EntryKind kind, Type valueType)
        {
            list.Add(new FieldEntry
            {
                Key = key,
                Label = key,
                Kind = kind,
                ValueType = valueType,
                Get = () => f.GetValue(holder),
                Set = v =>
                {
                    if (f.FieldType.IsEnum)
                    {
                        f.SetValue(holder, Enum.ToObject(f.FieldType, v is string ? Enum.Parse(f.FieldType, (string)v, true) : v));
                        return;
                    }
                    f.SetValue(holder, v);
                }
            });
        }

        private static void AddStruct(List<FieldEntry> list, object holder, FieldInfo f, string key, string component)
        {
            FieldInfo comp = f.FieldType.GetField(component);
            list.Add(new FieldEntry
            {
                Key = key + "." + component,
                Label = key + "." + component,
                Kind = EntryKind.Number,
                ValueType = comp.FieldType,
                Get = () =>
                {
                    object s = f.GetValue(holder);
                    return comp.GetValue(s);
                },
                Set = v =>
                {
                    object s = f.GetValue(holder);
                    comp.SetValue(s, v);
                    f.SetValue(holder, s);
                }
            });
        }
    }
}
