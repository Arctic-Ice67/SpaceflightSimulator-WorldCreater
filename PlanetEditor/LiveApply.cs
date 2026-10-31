using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using SFS;
using SFS.World;
using SFS.World.Maps;
using SFS.World.PlanetModules;
using SFS.World.Terrain;
using SFS.WorldBase;
using UnityEngine;

namespace PlanetEditor
{
    /// <summary>把改过的 PlanetData 立刻套回运行时（数值 / 材质 / 地形网格）。</summary>
    internal static class LiveApply
    {
        private static readonly MethodInfo MCreateTerrain = Find("CreateTerrainMaterial");
        private static readonly MethodInfo MCreateWater = Find("CreateWaterMaterial");
        private static readonly MethodInfo MCreateAtmo = Find("CreateAtmosphereMaterial");
        private static readonly MethodInfo MCreateClouds = Find("CreateFrontCloudsMaterial");
        private static readonly MethodInfo MCreateRings = Find("CreateRingsMaterial");
        private static readonly MethodInfo MGetRepeat = Find("GetRepeat");

        private static MethodInfo Find(string name)
        {
            try
            {
                return typeof(Planet).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public);
            }
            catch (Exception e)
            {
                Log.Error("reflect " + name + ": " + e.Message);
                return null;
            }
        }

        public static void Apply(Planet planet)
        {
            if (planet == null || planet.data == null) return;
            PlanetData data = planet.data;
            PlanetLoader loader = Base.planetLoader;
            if (loader == null) return;
            var log = new MsgNone();

            Material oldTerrain = planet.terrainMaterial;
            Material oldWater = planet.waterMaterial;
            Material oldAtmo = planet.atmosphereMaterial;
            Material oldClouds = planet.frontCloudsMaterial;
            Material oldRings = planet.ringsMaterial;

            // 1) 地形：公共分母 + 地形/水材质 + 采样器 + 最大地形高度
            if (data.hasTerrain && data.terrain != null)
            {
                TerrainModule tt = data.terrain;
                try
                {
                    planet.commonDenominator = ComputeCommonDenominator(planet, tt, data);
                }
                catch (Exception e)
                {
                    Log.Error("commonDenominator: " + e);
                }

                if (MCreateTerrain != null && tt.TERRAIN_TEXTURE_DATA != null)
                {
                    var m = (Material)MCreateTerrain.Invoke(planet, new object[] { tt.TERRAIN_TEXTURE_DATA, loader.terrainShader, log });
                    if (m != null)
                    {
                        m.SetColor("_Fog", Color.clear);
                        planet.terrainMaterial = m;
                    }
                }
                if (data.hasWater && data.water != null && MCreateWater != null)
                {
                    var m = (Material)MCreateWater.Invoke(planet, new object[] { data.water, loader.waterShader, log });
                    if (m != null) planet.waterMaterial = m;
                }
                try
                {
                    tt.SetupSamplers(planet.codeName, log);
                }
                catch (Exception e)
                {
                    Log.Error("setup samplers: " + e);
                }
                try
                {
                    planet.maxTerrainHeight = tt.GetMaxTerrainHeight(planet) + 200.0;
                }
                catch (Exception e)
                {
                    Log.Error("maxTerrainHeight: " + e);
                }
            }

            // 2) 其它视觉材质
            if (data.hasAtmosphereVisuals && data.atmosphereVisuals != null && MCreateAtmo != null)
            {
                var m = (Material)MCreateAtmo.Invoke(planet, new object[] { data.atmosphereVisuals, loader.atmosphereShader, log });
                if (m != null) planet.atmosphereMaterial = m;
            }
            if (data.hasFrontClouds && data.frontClouds != null && MCreateClouds != null)
            {
                var m = (Material)MCreateClouds.Invoke(planet, new object[] { data.frontClouds, loader.frontCloudsShader, log });
                if (m != null) planet.frontCloudsMaterial = m;
            }
            if (data.hasRings && data.rings != null && MCreateRings != null)
            {
                var m = (Material)MCreateRings.Invoke(planet, new object[] { data.rings, loader.ringsShader, log });
                if (m != null) planet.ringsMaterial = m;
            }

            // 3) 质量 / 轨道 / SOI
            try
            {
                planet.mass = Kepler.GetMass(data.basics.gravity, data.basics.radius);
                planet.SetupInteractions(loader.planets);
                planet.SetupDepthAndSatelliteIndex();
            }
            catch (Exception e)
            {
                Log.Error("orbit/mass: " + e);
            }

            // 4) 重建这颗星球的环境对象（地形网格、大气、云、环）
            try
            {
                RebuildEnvironment(planet);
            }
            catch (Exception e)
            {
                Log.Error("rebuild environment: " + e);
            }

            // 5) 销毁旧材质
            DestroyMaterial(oldTerrain);
            DestroyMaterial(oldWater);
            DestroyMaterial(oldAtmo);
            DestroyMaterial(oldClouds);
            DestroyMaterial(oldRings);
        }

        /// <summary>地图（M）上的大气圈/行星环是按加载时的标记生成的，删模块后要手动同步。</summary>
        public static void SyncMapEnvironment(Planet p)
        {
            if (p == null || p.mapHolder == null) return;
            Transform holder = p.mapHolder;
            try
            {
                // 大气圈
                Transform atmo = holder.Find("Atmosphere");
                if (p.HasAtmospherePhysics)
                {
                    if (atmo == null) atmo = CloneMapChild(p, "Atmosphere");
                    if (atmo != null)
                    {
                        atmo.gameObject.SetActive(true);
                        double r = (p.Radius + p.AtmosphereHeightPhysics) / 1000.0;
                        atmo.localScale = Vector3.one * (float)r;
                    }
                }
                else if (atmo != null)
                {
                    atmo.gameObject.SetActive(false);
                }

                // 行星环
                Transform rings = holder.Find("Rings");
                if (p.HasRings)
                {
                    if (rings == null) rings = CloneMapChild(p, "Rings");
                    if (rings != null) rings.gameObject.SetActive(true);
                }
                else if (rings != null)
                {
                    rings.gameObject.SetActive(false);
                }
            }
            catch (Exception e)
            {
                Log.Error("sync map env: " + e);
            }
        }

        /// <summary>从别的星球的地图节点里复制一个同名子物体（用于新启用的模块）。</summary>
        private static Transform CloneMapChild(Planet target, string childName)
        {
            try
            {
                foreach (Planet q in Base.planetLoader.planets.Values)
                {
                    if (q == null || q == target || q.mapHolder == null) continue;
                    Transform src = q.mapHolder.Find(childName);
                    if (src == null) continue;
                    GameObject go = UnityEngine.Object.Instantiate(src.gameObject, target.mapHolder);
                    go.name = childName;
                    go.transform.localPosition = Vector3.zero;
                    return go.transform;
                }
            }
            catch (Exception e)
            {
                Log.Error("clone map child: " + e);
            }
            return null;
        }
        /// <summary>只重建地形网格（刷地形时用，比完整 Apply 便宜得多）。</summary>
        public static void ApplyTerrainOnly(Planet planet)
        {
            if (planet == null || planet.data == null) return;
            try
            {
                if (planet.data.hasTerrain && planet.data.terrain != null)
                    planet.maxTerrainHeight = planet.data.terrain.GetMaxTerrainHeight(planet) + 200.0;
            }
            catch (Exception e)
            {
                Log.Error("terrain height: " + e);
            }
            try
            {
                RebuildEnvironment(planet);
            }
            catch (Exception e)
            {
                Log.Error("rebuild terrain: " + e);
            }
        }
        private static void DestroyMaterial(Material m)
        {
            try
            {
                if (m != null) UnityEngine.Object.Destroy(m);
            }
            catch { }
        }

        private static int ComputeCommonDenominator(Planet planet, TerrainModule tt, PlanetData data)
        {
            if (MGetRepeat == null) return planet.commonDenominator;
            int a = AbsInt(Rep(planet, new Vector2(100f, 100f)));
            int b = AbsInt(Rep(planet, tt.TERRAIN_TEXTURE_DATA.surfaceTextureSize_A));
            int c = AbsInt(Rep(planet, tt.TERRAIN_TEXTURE_DATA.surfaceTextureSize_B));
            int d = AbsInt(Rep(planet, tt.TERRAIN_TEXTURE_DATA.terrainTextureSize_C));
            int e = (data.hasWater && data.water != null) ? AbsInt(Rep(planet, data.water.wavesSize)) : int.MaxValue;
            int min = new[] { a, b, c, d, e }.Min();
            return Mathf.CeilToInt((float)min / 5f);
        }

        private static int Rep(Planet planet, Vector2 size)
        {
            return (int)((Vector2)MGetRepeat.Invoke(planet, new object[] { size, false })).x;
        }

        private static int AbsInt(int v)
        {
            return v == int.MinValue ? int.MaxValue : Mathf.Abs(v);
        }

        private static float _lastMapCheck = -1f;

        /// <summary>给已有地图节点、但缺 MapPlanet 组件的星球补组件（没有节点的先跳过，否则地图会空引用）。</summary>
        public static void EnsureMapPlanets()
        {
            try
            {
                if (Time.unscaledTime - _lastMapCheck < 1.0f) return;
                _lastMapCheck = Time.unscaledTime;

                MapEnvironment mapEnv = Map.environment;
                if (mapEnv == null) return;
                PlanetLoader loader = Base.planetLoader;
                if (loader == null || loader.planets == null) return;

                int added = 0;
                foreach (Planet q in loader.planets.Values)
                {
                    if (q == null || q.mapPlanet != null) continue;
                    if (q.mapHolder == null) continue;   // 没节点 = 地图画不了它，先不挂组件
                    MapPlanet mp = mapEnv.gameObject.AddComponent<MapPlanet>();
                    if (mp == null) continue;
                    mp.planet = q;
                    q.mapPlanet = mp;
                    added++;
                }
                if (added > 0) Log.Info("map planet components added: " + added);
            }
            catch (Exception e)
            {
                Log.Error("ensure map planets: " + e);
            }
        }

        /// <summary>新建星球后重建地图节点：把旧节点藏起来（不销毁！池里的 LineRenderer 挂在上面），
        /// 再让游戏自己 CreateEnvironments() 给包括新星球在内的所有星球建节点。</summary>
        public static bool RebuildMapForNewPlanet(Planet added)
        {
            try
            {
                MapEnvironment mapEnv = Map.environment;
                PlanetLoader loader = Base.planetLoader;
                if (mapEnv == null || loader == null || loader.planets == null) return false;

                // 1) 旧节点隐藏（绝不 Destroy，否则会销毁池化的 LineRenderer）
                foreach (Planet q in loader.planets.Values)
                {
                    if (q == null || q == added || q.mapHolder == null) continue;
                    q.mapHolder.gameObject.SetActive(false);
                }

                // 2) 让游戏重建所有星球的地图节点
                mapEnv.CreateEnvironments();

                // 3) 补上缺的 MapPlanet 组件（此时新星球已有 mapHolder）
                int addedComp = 0;
                foreach (Planet q in loader.planets.Values)
                {
                    if (q == null || q.mapPlanet != null || q.mapHolder == null) continue;
                    MapPlanet mp = mapEnv.gameObject.AddComponent<MapPlanet>();
                    if (mp == null) continue;
                    mp.planet = q;
                    q.mapPlanet = mp;
                    addedComp++;
                }
                Log.Info("map rebuilt for new planet " + (added != null ? added.codeName : "?") + " (components +" + addedComp + ")");
                return true;
            }
            catch (Exception e)
            {
                Log.Error("rebuild map for new planet: " + e);
                return false;
            }
        }

        /// <summary>删星球时把地图节点藏起来——绝对不能 Destroy：
        /// 地图的 LineRenderer 是池化复用的，销毁会被 DrawReset 二次访问导致崩溃。</summary>
        public static void DetachMapEnvironment(Planet planet)
        {
            try
            {
                if (planet == null) return;
                if (planet.mapHolder != null)
                    planet.mapHolder.gameObject.SetActive(false);
                if (planet.mapPlanet != null)
                    planet.mapPlanet.enabled = false;
                Log.Info("map node hidden for " + planet.codeName);
            }
            catch (Exception e) { Log.Error("detach map: " + e); }
        }

        /// <summary>为一个星球新建环境对象（地形/大气/云/环），返回它（已挂到 WorldEnvironment 下）。</summary>
        public static SFS.World.Environment CreateEnvironmentFor(Planet planet, WorldEnvironment we)
        {
            double t = WorldTime.main != null ? WorldTime.main.worldTime : 0.0;
            Double2 viewSS = WorldView.main.ViewLocation.GetSolarSystemPosition(t);
            Double2 selfSS = planet.GetSolarSystemPosition(t);
            var viewPos = new Double3(
                viewSS.x - selfSS.x,
                viewSS.y - selfSS.y,
                (float)WorldView.main.viewDistance);

            var holder = new GameObject(planet.codeName + " Environment").transform;
            holder.parent = we.transform;

            var env = new SFS.World.Environment { planet = planet, holder = holder };

            if (planet.data.hasTerrain && planet.data.terrain != null)
            {
                TerrainModule.RockData rockData = planet.data.terrain.rocks;
                Transform rockPrefab = null;
                if (rockData != null && we.rockPrefabs != null)
                {
                    for (int i = 0; i < we.rockPrefabs.Length; i++)
                    {
                        if (we.rockPrefabs[i] != null && we.rockPrefabs[i].name == rockData.rockType)
                        {
                            rockPrefab = we.rockPrefabs[i];
                            break;
                        }
                    }
                }
                env.terrain = DynamicTerrain.Create(planet, viewPos, null, 1f, 1f, "Default",
                    (planet.terrainMaterial, planet.waterMaterial), true, we.chunkPrefab,
                    rockPrefab != null ? rockData : null, rockPrefab);
                if (env.terrain != null) env.terrain.transform.parent = holder;
            }
            if (planet.HasAtmosphereVisuals)
                env.atmosphere = Atmosphere.Create(planet, holder, we.atmospherePrefab);
            if (planet.HasFrontClouds)
                env.frontClouds = FrontClouds.Create(planet, holder, we.atmospherePrefab, planet.frontCloudsMaterial);
            if (planet.HasRings)
                env.rings = Rings.Create(planet, holder, we.atmospherePrefab, planet.ringsMaterial);

            bool scaled = false;
            try { scaled = WorldView.main.scaledSpace.Value; } catch { }
            if (scaled)
            {
                holder.localScale = Vector3.one / 10000f;
                if (env.terrain != null) env.terrain.SetLayer("Scaled Space");
                if (env.atmosphere != null) env.atmosphere.SetLayer("Scaled Space");
                if (env.frontClouds != null) env.frontClouds.SetLayer("Scaled Space");
                if (env.rings != null) env.rings.SetLayer("Scaled Space");
            }
            return env;
        }

        /// <summary>给实时新建的星球补一个环境（否则游戏每帧调 Atmosphere.Update 会空引用）。</summary>
        public static bool AttachEnvironment(Planet planet)
        {
            try
            {
                WorldEnvironment we = GameManager.main != null ? GameManager.main.environment : null;
                if (we == null || we.environments == null || planet == null) return false;

                for (int i = 0; i < we.environments.Length; i++)
                    if (we.environments[i] != null && we.environments[i].planet == planet) return true;

                SFS.World.Environment env = CreateEnvironmentFor(planet, we);
                var list = new List<SFS.World.Environment>(we.environments);
                list.Add(env);
                we.environments = list.ToArray();
                Log.Info("environment attached for " + planet.codeName);
                return true;
            }
            catch (Exception e)
            {
                Log.Error("attach environment: " + e);
                return false;
            }
        }

        /// <summary>删星球前先把环境摘掉（销毁 holder 并从数组里移除），否则会留下悬空引用。</summary>
        public static bool DetachEnvironment(Planet planet)
        {
            try
            {
                WorldEnvironment we = GameManager.main != null ? GameManager.main.environment : null;
                if (we == null || we.environments == null || planet == null) return false;

                var list = new List<SFS.World.Environment>(we.environments);
                for (int i = list.Count - 1; i >= 0; i--)
                {
                    if (list[i] == null || list[i].planet != planet) continue;
                    try { if (list[i].holder != null) UnityEngine.Object.Destroy(list[i].holder.gameObject); } catch { }
                    list.RemoveAt(i);
                }
                we.environments = list.ToArray();
                Log.Info("environment detached for " + planet.codeName);
                return true;
            }
            catch (Exception e)
            {
                Log.Error("detach environment: " + e);
                return false;
            }
        }

        private static void RebuildEnvironment(Planet planet)
        {
            WorldEnvironment we = GameManager.main != null ? GameManager.main.environment : null;
            if (we == null || we.environments == null) return;

            int idx = -1;
            for (int i = 0; i < we.environments.Length; i++)
            {
                if (we.environments[i] != null && we.environments[i].planet == planet)
                {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) return;

            SFS.World.Environment old = we.environments[idx];
            if (old != null && old.holder != null) UnityEngine.Object.Destroy(old.holder.gameObject);

            double t = WorldTime.main != null ? WorldTime.main.worldTime : 0.0;
            Double2 viewSS = WorldView.main.ViewLocation.GetSolarSystemPosition(t);
            Double2 selfSS = planet.GetSolarSystemPosition(t);
            var viewPos = new Double3(
                viewSS.x - selfSS.x,
                viewSS.y - selfSS.y,
                (float)WorldView.main.viewDistance);

            var holder = new GameObject(planet.codeName + " Environment").transform;
            holder.parent = we.transform;

            var env = new SFS.World.Environment { planet = planet, holder = holder };

            if (planet.data.hasTerrain)
            {
                TerrainModule.RockData rockData = planet.data.terrain != null ? planet.data.terrain.rocks : null;
                Transform rockPrefab = null;
                if (rockData != null && we.rockPrefabs != null)
                {
                    for (int i = 0; i < we.rockPrefabs.Length; i++)
                    {
                        if (we.rockPrefabs[i] != null && we.rockPrefabs[i].name == rockData.rockType)
                        {
                            rockPrefab = we.rockPrefabs[i];
                            break;
                        }
                    }
                }
                env.terrain = DynamicTerrain.Create(planet, viewPos, null, 1f, 1f, "Default",
                    (planet.terrainMaterial, planet.waterMaterial), true, we.chunkPrefab,
                    rockPrefab != null ? rockData : null, rockPrefab);
                env.terrain.transform.parent = holder;
            }
            if (planet.HasAtmosphereVisuals)
                env.atmosphere = Atmosphere.Create(planet, holder, we.atmospherePrefab);
            if (planet.HasFrontClouds)
                env.frontClouds = FrontClouds.Create(planet, holder, we.atmospherePrefab, planet.frontCloudsMaterial);
            if (planet.HasRings)
                env.rings = Rings.Create(planet, holder, we.atmospherePrefab, planet.ringsMaterial);

            we.environments[idx] = env;

            bool scaled = false;
            try { scaled = WorldView.main.scaledSpace.Value; } catch { }
            if (scaled)
            {
                holder.localScale = Vector3.one / 10000f;
                if (env.terrain != null) env.terrain.SetLayer("Scaled Space");
                if (env.atmosphere != null) env.atmosphere.SetLayer("Scaled Space");
                if (env.frontClouds != null) env.frontClouds.SetLayer("Scaled Space");
                if (env.rings != null) env.rings.SetLayer("Scaled Space");
            }
        }
    }
}
