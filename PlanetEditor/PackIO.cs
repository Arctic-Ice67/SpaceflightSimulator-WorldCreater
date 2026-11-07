using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using SFS;
using SFS.Parsers.Json;
using SFS.World;
using SFS.World.PlanetModules;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>读写“星球包”（Custom Solar Systems/&lt;名字&gt;/Planet Data/&lt;星球&gt;.txt）。</summary>
    internal static class PackIO
    {
        public static IFolder EnsurePack()
        {
            WorldBaseManager wb = Base.worldBase;
            if (wb == null || wb.settings == null) return null;

            WorldSettings settings = wb.settings;
            string name = settings.solarSystem != null ? settings.solarSystem.name : null;
            bool ours = string.IsNullOrEmpty(name);

            if (ours)
            {
                string world = wb.paths != null ? wb.paths.worldName : null;
                name = Sanitize("Edited " + (string.IsNullOrEmpty(world) ? "World" : world));

                IFolder created = FileLocations.SolarSystemsFolder.GetFolder(name).Create();
                WriteImportSettings(created);
                try
                {
                    if (Base.planetLoader != null && Base.planetLoader.spaceCenter != null)
                        created.GetFile("Space_Center_Data.txt").WriteText(JsonWrapper.ToJson(Base.planetLoader.spaceCenter, true));
                }
                catch (Exception e)
                {
                    Log.Error("space center: " + e.Message);
                }
                created.GetFile("Version.txt").WriteText(Application.version);

                settings.solarSystem = new SolarSystemReference(name);
                try
                {
                    if (wb.paths != null) wb.paths.SaveWorldSettings(settings);
                }
                catch (Exception e)
                {
                    Log.Error("save world settings: " + e.Message);
                }
                Log.Info("created planet pack '" + name + "' for world '" + world + "'");
                EditSession.Report("Planet pack created: " + name);
            }

            IFolder folder = FileLocations.SolarSystemsFolder.GetFolder(name);
            if (!folder.Exists()) folder.Create();

            // 我们自己建的包：必须是"完整一套星球 + includeDefaultPlanets=false"，
            // 否则加载器会把同名文件当成重复直接丢掉（LoadPlanets_Public 里 ContainsKey 就 skip）。
            if (IsOurPack(folder))
            {
                WriteImportSettings(folder);
                folder.GetFolder("Texture Data").Create();
                folder.GetFolder("Heightmap Data").Create();
                try
                {
                    ExportAllPlanets(folder);
                    ExportHeightmaps(folder);
                }
                catch (Exception e)
                {
                    Log.Error("export planets: " + e);
                }
            }
            return folder;
        }

        private static void WriteImportSettings(IFolder pack)
        {
            PackMeta.Load(pack);
            var import = new SolarSystemSettings
            {
                includeDefaultPlanets = false,
                includeDefaultHeightmaps = true,
                includeDefaultTextures = true,
                hideStarsInAtmosphere = true,
                authorName = PackMeta.Author,
                version = PackMeta.Version,
                description = PackMeta.Description
            };
            pack.GetFile("Import_Settings.txt").WriteText(JsonWrapper.ToJson(import, true));
        }

        /// <summary>对外暴露：重写导入设置（改了作者信息后调用）。</summary>
        public static void WriteSettingsPublic(IFolder pack)
        {
            if (pack == null) return;
            try { WriteImportSettings(pack); }
            catch (Exception e) { Log.Error("write import settings: " + e.Message); }
        }

        private static bool IsOurPack(IFolder pack)
        {
            try
            {
                IFile f = pack.GetFile("Import_Settings.txt");
                if (!f.Exists()) return false;
                SolarSystemSettings s;
                if (!JsonWrapper.TryLoadJson<SolarSystemSettings>(f, out s) || s == null) return false;
                return s.authorName == "World Creater" || s.authorName == "Planet Editor";   // 兼容旧包
            }
            catch
            {
                return false;
            }
        }

        /// <summary>把当前太阳系里所有星球写成文件（已存在的不覆盖，保留玩家改动）。</summary>
        private static void ExportAllPlanets(IFolder pack)
        {
            PlanetLoader loader = Base.planetLoader;
            if (loader == null || loader.planets == null) return;
            IFolder dataFolder = pack.GetFolder("Planet Data");
            dataFolder.Create();

            int written = 0;
            foreach (Planet p in loader.planets.Values)
            {
                try
                {
                    if (p == null || p.data == null) continue;
                    IFile f = dataFolder.GetFile(p.codeName + ".txt");
                    if (f.Exists()) continue;
                    PlanetData copy = Clone(p.data);
                    Unscale(copy);
                    f.WriteText(JsonWrapper.ToJson(copy, true));
                    written++;
                }
                catch (Exception e)
                {
                    Log.Error("export " + (p != null ? p.codeName : "?") + ": " + e.Message);
                }
            }
            if (written > 0) Log.Info("exported " + written + " planet files into " + pack.Name);
        }

        public static string Save(Planet planet)
        {
            if (planet == null || planet.data == null) return null;
            IFolder pack = EnsurePack();
            if (pack == null) return null;

            PlanetData copy = Clone(planet.data);
            Unscale(copy);

            IFolder dataFolder = pack.GetFolder("Planet Data");
            dataFolder.Create();
            IFile file = dataFolder.GetFile(planet.codeName + ".txt");
            /* 覆盖前先留一份自动备份，改坏了能退回上一版 */
            PackBackup.KeepAuto(pack, "Planet Data", planet.codeName + ".txt");
            file.WriteText(JsonWrapper.ToJson(copy, true));
            Log.Info("wrote " + file.Path);
            SaveBrush(pack);
            return file.Path;
        }

        /// <summary>把当前包打成 zip，方便分享或备份（跳过备份目录和旧 zip）。</summary>
        public static string ExportZip(IFolder pack, string zipPath)
        {
            if (pack == null) return null;
            try
            {
                IFile any = pack.GetFile("Import_Settings.txt");
                string packDir = null;
                try { packDir = System.IO.Path.GetDirectoryName(any.Path); } catch { }
                if (string.IsNullOrEmpty(packDir) || !System.IO.Directory.Exists(packDir)) return "pack path not found";
                if (string.IsNullOrEmpty(zipPath)) zipPath = System.IO.Path.Combine(packDir, pack.Name + ".zip");
                if (System.IO.File.Exists(zipPath)) System.IO.File.Delete(zipPath);

                string root = System.IO.Path.GetFullPath(packDir);
                using (var zip = System.IO.Compression.ZipFile.Open(zipPath, System.IO.Compression.ZipArchiveMode.Create))
                {
                    foreach (string file in System.IO.Directory.GetFiles(root, "*", System.IO.SearchOption.AllDirectories))
                    {
                        string full = System.IO.Path.GetFullPath(file);
                        string rel = full.Substring(root.Length).TrimStart('\\', '/');
                        // 备份目录和 zip 本身不要塞进去
                        if (rel.StartsWith("Backup\\", StringComparison.OrdinalIgnoreCase)) continue;
                        if (rel.StartsWith("Backup/", StringComparison.OrdinalIgnoreCase)) continue;
                        if (rel.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) continue;
                        System.IO.Compression.ZipFileExtensions.CreateEntryFromFile(zip, full, rel.Replace('\\', '/'));
                    }
                }
                Log.Info("exported pack zip: " + zipPath);
                return zipPath;
            }
            catch (Exception e) { Log.Error("export zip: " + e.Message); return null; }
        }

        /// <summary>把当前加载到的全部高度图补写进包里（缺哪个补哪个），让包自包含。</summary>
        public static void ExportHeightmaps(IFolder pack)
        {
            try
            {
                PlanetLoader loader = Base.planetLoader;
                if (loader == null || loader.heightmaps == null) return;
                IFolder dir = pack.GetFolder("Heightmap Data");
                dir.Create();
                int n = 0;
                foreach (var kv in loader.heightmaps)
                {
                    HeightMap hm = kv.Value;
                    if (hm == null || hm.points == null || hm.points.Length < 2) continue;
                    string name = kv.Key;
                    if (!name.EndsWith(".txt", StringComparison.OrdinalIgnoreCase)) name += ".txt";
                    IFile hf = dir.GetFile(name);
                    if (hf.Exists()) continue;
                    var sb = new System.Text.StringBuilder(hm.points.Length * 10 + 32);
                    sb.Append("{\"points\":[");
                    for (int i = 0; i < hm.points.Length; i++)
                    {
                        if (i > 0) sb.Append(',');
                        sb.Append(hm.points[i].ToString("R", System.Globalization.CultureInfo.InvariantCulture));
                    }
                    sb.Append("]}");
                    hf.WriteText(sb.ToString());
                    n++;
                }
                if (n > 0) Log.Info("exported " + n + " heightmaps into pack");
            }
            catch (Exception e)
            {
                Log.Error("export heightmaps: " + e);
            }
        }
        /// <summary>把笔刷曲线写成 Heightmap Data/PlanetEditorBrush.txt（key 带 .txt，和加载器一致）。</summary>
        public static void SaveBrush(IFolder pack)
        {
            try
            {
                HeightMap curve = TerrainBrush.Curve;
                if (curve == null || curve.points == null) return;
                IFolder dir = pack.GetFolder("Heightmap Data");
                dir.Create();
                var sb = new System.Text.StringBuilder(curve.points.Length * 10 + 32);
                sb.Append("{\"points\":[");
                for (int i = 0; i < curve.points.Length; i++)
                {
                    if (i > 0) sb.Append(',');
                    sb.Append(curve.points[i].ToString("R", System.Globalization.CultureInfo.InvariantCulture));
                }
                sb.Append("]}");
                IFile f = dir.GetFile(TerrainBrush.CurveName);
                f.WriteText(sb.ToString());
                Log.Info("wrote brush heightmap " + f.Path + " (" + curve.points.Length + " pts)");
            }
            catch (Exception e)
            {
                Log.Error("save brush: " + e);
            }
        }
        public static bool ReloadPlanet(Planet planet)
        {
            if (planet == null) return false;
            try
            {
                IFolder pack = null;
                WorldBaseManager wb = Base.worldBase;
                if (wb != null && wb.settings != null && wb.settings.solarSystem != null &&
                    !string.IsNullOrEmpty(wb.settings.solarSystem.name))
                {
                    pack = FileLocations.SolarSystemsFolder.GetFolder(wb.settings.solarSystem.name);
                }
                if (pack == null || !pack.Exists()) return false;

                IFile file = pack.GetFolder("Planet Data").GetFile(planet.codeName + ".txt");
                if (!file.Exists()) return false;

                PlanetData data = JsonWrapper.FromJson<PlanetData>(file.ReadText());
                if (data == null) return false;

                if (wb.settings.difficulty != null) wb.settings.difficulty.ScalePlanetData(data);
                planet.data = data;
                FieldTree.Invalidate();
                Log.Info("reloaded " + file.Path);
                return true;
            }
            catch (Exception e)
            {
                Log.Error("reload: " + e);
                return false;
            }
        }

        public static void RescanTextures()
        {
            try
            {
                PlanetLoader loader = Base.planetLoader;
                if (loader == null) return;
                IFolder pack = null;
                WorldBaseManager wb = Base.worldBase;
                if (wb != null && wb.settings != null && wb.settings.solarSystem != null && !string.IsNullOrEmpty(wb.settings.solarSystem.name))
                    pack = FileLocations.SolarSystemsFolder.GetFolder(wb.settings.solarSystem.name);
                if (pack == null || !pack.Exists()) return;

                IFolder tex = pack.GetFolder("Texture Data");
                if (!tex.Exists()) return;

                int n = 0;
                foreach (IFile f in tex.GetFiles())
                {
                    string ext = (f.GetExtension() ?? "").ToLowerInvariant();
                    if (ext != "png" && ext != "jpg" && ext != "jpeg") continue;
                    try
                    {
                        var t = new Texture2D(0, 0);
                        if (TryLoadImage(t, f.ReadBytes()))
                        {
                            loader.textures[f.GetNameWithoutExtension()] = t;
                            n++;
                        }
                    }
                    catch (Exception e)
                    {
                        Log.Error("texture " + f.Name + ": " + e.Message);
                    }
                }
                Log.Info("textures loaded: " + n);
            }
            catch (Exception e)
            {
                Log.Error("rescan textures: " + e);
            }
        }

        private static PlanetData Clone(PlanetData data)
        {
            return JsonWrapper.FromJson<PlanetData>(JsonWrapper.ToJson(data, true));
        }

        /// <summary>UnityEngine.ImageConversionModule 是 netstandard2.1 的，编译期不能引用，走反射调用。</summary>
        private static bool TryLoadImage(Texture2D tex, byte[] bytes)
        {
            try
            {
                Type t = Type.GetType("UnityEngine.ImageConversion, UnityEngine.ImageConversionModule");
                if (t == null) return false;
                MethodInfo m = t.GetMethod("LoadImage", new[] { typeof(Texture2D), typeof(byte[]) });
                if (m == null) return false;
                object r = m.Invoke(null, new object[] { tex, bytes });
                return r is bool && (bool)r;
            }
            catch (Exception e)
            {
                Log.Error("LoadImage: " + e.Message);
                return false;
            }
        }

        /// <summary>对外暴露：写盘前把难度缩放还原。</summary>
        public static void UnscalePublic(PlanetData d) { Unscale(d); }

        private static void Unscale(PlanetData d)
        {
            try
            {
                WorldBaseManager wb = Base.worldBase;
                if (wb == null || wb.settings == null || wb.settings.difficulty == null) return;
                Difficulty diff = wb.settings.difficulty;
                Difficulty.DifficultyType type = diff.difficulty;

                double radiusScale = Safe(diff.RadiusScale(d));
                double atmoScale = Safe(diff.AtmosphereScale(d));
                double gravityScale = Safe(Get(d.basics.gravityDifficultyScale, type, 1.0));
                double curveScale = Safe(Get(d.atmospherePhysics != null ? d.atmospherePhysics.curveScale : null, type, diff.DefaultAtmoCurveScale));
                double smaScale = Safe(Get(d.orbit != null ? d.orbit.smaDifficultyScale : null, type, diff.DefaultSmaScale));
                double soiScale = Safe(Get(d.orbit != null ? d.orbit.soiDifficultyScale : null, type, 1.0));

                d.basics.radius /= radiusScale;
                d.basics.gravity /= gravityScale;
                d.basics.timewarpHeight /= atmoScale;

                if (d.hasAtmospherePhysics && d.atmospherePhysics != null)
                {
                    d.atmospherePhysics.height /= atmoScale;
                    d.atmospherePhysics.curve /= curveScale;
                }
                if (d.hasAtmosphereVisuals && d.atmosphereVisuals != null)
                {
                    d.atmosphereVisuals.GRADIENT.height /= atmoScale;
                    d.atmosphereVisuals.GRADIENT.positionZ = (int)(d.atmosphereVisuals.GRADIENT.positionZ / atmoScale);
                    d.atmosphereVisuals.CLOUDS.startHeight /= (float)atmoScale;
                    d.atmosphereVisuals.CLOUDS.width /= (float)atmoScale;
                    d.atmosphereVisuals.CLOUDS.height /= (float)atmoScale;
                    if (d.atmosphereVisuals.FOG != null && d.atmosphereVisuals.FOG.keys != null)
                    {
                        for (int i = 0; i < d.atmosphereVisuals.FOG.keys.Length; i++)
                            d.atmosphereVisuals.FOG.keys[i].distance /= (float)atmoScale;
                    }
                }
                if (d.hasPostProcessing && d.postProcessing != null && d.postProcessing.keys != null)
                {
                    for (int i = 0; i < d.postProcessing.keys.Length; i++)
                        d.postProcessing.keys[i].height /= (float)atmoScale;
                }
                if (d.hasFrontClouds && d.frontClouds != null)
                {
                    d.frontClouds.positionZ /= (float)atmoScale;
                    d.frontClouds.height /= (float)atmoScale;
                    d.frontClouds.fadeZoneHeight /= (float)atmoScale;
                }
                if (d.hasRings && d.rings != null)
                {
                    d.rings.startRadius /= radiusScale;
                    d.rings.endRadius /= radiusScale;
                    d.rings.positionZ /= (float)radiusScale;
                }
                if (d.hasOrbit && d.orbit != null)
                {
                    d.orbit.semiMajorAxis /= smaScale;
                    d.orbit.multiplierSOI /= soiScale;
                }
            }
            catch (Exception e)
            {
                Log.Error("unscale: " + e.Message);
            }
        }

        private static double Get(Dictionary<Difficulty.DifficultyType, double> dict, Difficulty.DifficultyType type, double fallback)
        {
            double v;
            if (dict != null && dict.TryGetValue(type, out v) && Math.Abs(v) > 1e-12) return v;
            return fallback;
        }

        private static double Safe(double v)
        {
            return Math.Abs(v) < 1e-12 ? 1.0 : v;
        }

        private static string Sanitize(string name)
        {
            foreach (char c in Path.GetInvalidFileNameChars())
                name = name.Replace(c, '_');
            return name.Trim();
        }
    }
}
