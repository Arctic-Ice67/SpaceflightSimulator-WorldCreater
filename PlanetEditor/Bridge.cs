using System;

using System.Collections.Concurrent;

using System.Collections.Generic;

using System.IO;

using System.Net;

using System.Net.Sockets;

using System.Text;

using System.Threading;

using SFS;
using SFS.World;
using SFS.Logs;
using SFS.World.Maps;
using SFS.Parsers.Json;
using SFS.World.PlanetModules;
using SFS.WorldBase;

using UnityEngine;


namespace PlanetEditor

{
    /// <summary>
    /// 给外部 C++ 面板用的接口层（TCP 127.0.0.1:41729，一行一条命令）。
    /// 所有 Unity / 星球操作都必须回主线程执行 —— 网络线程只收命令、入队。

    /// </summary>
    internal static class Bridge

    {
        public const int Port = 41729;


        private class Client

        {
            public TcpClient tcp;

            public NetworkStream stream;

            public StreamReader reader;
            public readonly object writeLock = new object();

        }

        private static TcpListener _listener;

        private static Thread _acceptThread;
        private static volatile bool _running;

        private static readonly ConcurrentQueue<Action> _queue = new ConcurrentQueue<Action>();

        private static readonly List<Client> _clients = new List<Client>();

        private static readonly object _clientsLock = new object();


        public static bool Running { get { return _running; } }

        public static int ClientCount { get { lock (_clientsLock) return _clients.Count; } }


        public static void Start()
        {
            if (_running) return;

            try
            {
                _listener = new TcpListener(IPAddress.Loopback, Port);

                _listener.Start();

                _running = true;

                _acceptThread = new Thread(AcceptLoop);

                _acceptThread.IsBackground = true;

                _acceptThread.Name = "PlanetEditorBridge";

                _acceptThread.Start();
                Log.Info("bridge: listening on 127.0.0.1:" + Port);

            }
            catch (Exception e)

            {
                Log.Error("bridge start: " + e.Message);
                _running = false;

            }
        }

        public static void Stop()
        {
            _running = false;

            try { if (_listener != null) _listener.Stop(); } catch { }

            lock (_clientsLock)

            {
                foreach (Client c in _clients) { try { c.tcp.Close(); } catch { } }

                _clients.Clear();

            }
            _listener = null;

        }

        private static void AcceptLoop()
        {
            while (_running)

            {
                try
                {
                    TcpClient tcp = _listener.AcceptTcpClient();

                    tcp.NoDelay = true;

                    var c = new Client

                    {
                        tcp = tcp,
                        stream = tcp.GetStream(),
                        reader = new StreamReader(tcp.GetStream(), Encoding.UTF8)

                    };
                    lock (_clientsLock) { _clients.Add(c); }

                    var t = new Thread(() => ClientLoop(c));

                    t.IsBackground = true;

                    t.Start();
                    Log.Info("bridge: client connected (" + ClientCount + ")");

                }
                catch (Exception e)

                {
                    if (_running) { Log.Error("bridge accept: " + e.Message); Thread.Sleep(300); }

                }
            }
        }

        private static void ClientLoop(Client c)

        {
            try
            {
                string line;

                while (_running && (line = c.reader.ReadLine()) != null)

                {
                    string cmd = line;

                    Client cc = c;

                    _queue.Enqueue(() => Execute(cc, cmd));

                }
            }
            catch { }
            lock (_clientsLock) { _clients.Remove(c); }

            try { c.tcp.Close(); } catch { }
            Log.Info("bridge: client disconnected (" + ClientCount + ")");

        }

        /// <summary>每帧由 Watcher 调用（主线程）。</summary>
        public static void Pump()
        {
            Action a;

            while (_queue.TryDequeue(out a))
            {
                try { a(); }
                catch (Exception e) { Log.Error("bridge cmd: " + e); }

            }
        }

        private static void Send(Client c, string text)

        {
            try
            {
                byte[] b = Encoding.UTF8.GetBytes(text + "\n");

                lock (c.writeLock) { c.stream.Write(b, 0, b.Length); c.stream.Flush(); }

            }
            catch { }
        }

        private static void SendMany(Client c, string header, List<string> lines)

        {
            try
            {
                var sb = new StringBuilder();

                sb.Append(header).Append('\n');

                for (int i = 0; i < lines.Count; i++) sb.Append(lines[i]).Append('\n');

                byte[] b = Encoding.UTF8.GetBytes(sb.ToString());

                lock (c.writeLock) { c.stream.Write(b, 0, b.Length); c.stream.Flush(); }

            }
            catch { }
        }

        // ---------------- 新建星球 / 质心工具 ----------------

        /// <summary>用模板复制一颗新星球（写进包的 Planet Data，重载世界后出现）。</summary>
        private static string CreatePlanetFromTemplate(string template, out string newName, out PlanetData outData)
        {
            newName = null;
            outData = null;
            Planet src = FindPlanet(template);
            PlanetData d = null;

            if (src != null && src.data != null)
            {
                d = JsonWrapper.FromJson<PlanetData>(JsonWrapper.ToJson(src.data, false));
                if (d == null) return "clone failed";
            }
            else
            {
                // 质心样板：没有对应星球时手工构造
                d = new PlanetData();
                d.version = "1.6";
                d.basics = new BasicModule
                {
                    radius = 10.0,
                    gravity = 300000.0,
                    timewarpHeight = 25000.0,
                    velocityArrowsHeight = 5000.0,
                    mapColor = new Color(0.7f, 0.7f, 0.7f, 0.5f),
                    significant = true,
                    rotateCamera = true
                };
                d.orbit = new OrbitModule { parent = "Sun", semiMajorAxis = 470861521430.0, eccentricity = 0.0, argumentOfPeriapsis = 0.0, direction = 1, multiplierSOI = 2.5 };
                d.logs = new LogsModule();
                d.landmarks = new List<LandmarkData>();
                Planet any = FindPlanet("Earth");
                if (any != null && any.data != null && any.data.postProcessing != null)
                    d.postProcessing = JsonWrapper.FromJson<PostProcessingModule>(JsonWrapper.ToJson(any.data.postProcessing, false));
            }

            // 新名字
            string baseName = (src != null && !string.IsNullOrEmpty(src.codeName)) ? src.codeName : "Barycenter";
            baseName += "_New";
            IFolder dir = PackIO.EnsurePack();
            if (dir == null) return "no pack";
            IFolder pd = dir.GetFolder("Planet Data");
            pd.Create();
            int n = 1;
            while (true)
            {
                string cand = baseName + n.ToString();
                if (!pd.GetFile(cand + ".txt").Exists()) { newName = cand; break; }
                n++;
                if (n > 999) return "too many";
            }

            // 轨道稍微外移，避免和母星重叠
            if (d.orbit != null)
            {
                if (src != null && src.data != null && src.data.orbit != null)
                    d.orbit.semiMajorAxis = src.data.orbit.semiMajorAxis * 1.18;
            }

            if (d.landmarks != null) d.landmarks.Clear();

            // 文件写"未缩放"的副本，内存里那份留给实时注册
            PlanetData saveCopy = JsonWrapper.FromJson<PlanetData>(JsonWrapper.ToJson(d, false));
            if (saveCopy != null) { try { PackIO.UnscalePublic(saveCopy); } catch { } }
            pd.GetFile(newName + ".txt").WriteText(JsonWrapper.ToJson(saveCopy ?? d, true));
            outData = d;
            Log.Info("created planet " + newName + " from " + template);
            return "ok";
        }
        /// <summary>把该星球的 SOI 撑到覆盖所有子天体的轨道（质心工具）。</summary>
        private static string FitSOI(Planet p)
        {
            if (p == null || p.data == null || p.data.orbit == null) return "该星球没有轨道，不能做质心";
            if (p.parentBody == null) return "该星球没有母天体";
            double maxChild = 0.0;
            foreach (Planet q in EditSession.AllPlanets())
            {
                if (q == null || q == p || q.data == null || q.data.orbit == null) continue;
                if (q.data.orbit.parent == p.codeName && q.data.orbit.semiMajorAxis > maxChild)
                    maxChild = q.data.orbit.semiMajorAxis;
            }
            if (maxChild <= 0.0) return "no children";


            double desired = maxChild * 1.15;
            if (p.SOI >= desired) return "already ok（现有 SOI 已覆盖子天体轨道）";
            double sma = p.data.orbit.semiMajorAxis;
            double baseSOI = Kepler.GetSphereOfInfluence(sma, p.mass, p.parentBody.mass, 1.0);
            if (baseSOI <= 0.0) return "bad soi";

            double mult = desired / baseSOI;
            if (mult < p.data.orbit.multiplierSOI) mult = p.data.orbit.multiplierSOI;   // 只放大不缩小
            if (mult > 500.0) mult = 500.0;
            p.data.orbit.multiplierSOI = mult;
            p.SOI = Kepler.GetSphereOfInfluence(sma, p.mass, p.parentBody.mass, mult);
            Log.Info("soi fit " + p.codeName + " mult=" + mult.ToString("F3") + " SOI=" + p.SOI.ToString("G4"));
            return "ok mult=" + mult.ToString("F3");
        }
        // ---------------- 命令 ----------------

        /// <summary>把当前星球的地形轮廓推给面板（180 个采样高度 + 半径 + 主色）。</summary>
        private static void SendShape(Client c, Planet p, int N = 180)

        {
            if (N < 16) N = 16;
            if (N > 8192) N = 8192;

            var angs = new double[N];

            for (int i = 0; i < N; i++) angs[i] = Math.PI * 2.0 * i / N;

            double[] hs;
            try { hs = p.GetTerrainHeightAtAngles(angs, false); }

            catch { hs = new double[N]; }

            if (hs == null || hs.Length < N) hs = new double[N];

            int cr = 150, cg = 150, cb = 150;

            try
            {
                Color mc = p.data.basics.mapColor;
                cr = (int)Mathf.Clamp(mc.r * 255f, 0, 255);

                cg = (int)Mathf.Clamp(mc.g * 255f, 0, 255);

                cb = (int)Mathf.Clamp(mc.b * 255f, 0, 255);

            }
            catch { }
            var sb = new StringBuilder();

            sb.Append("SHAPE ").Append(p.codeName).Append(' ').Append(N).Append(' ')

              .Append(p.Radius.ToString("R", System.Globalization.CultureInfo.InvariantCulture)).Append(' ')

              .Append(cr).Append(' ').Append(cg).Append(' ').Append(cb).Append('\n');

            for (int i = 0; i < N; i++)

                sb.Append(hs[i].ToString("F1", System.Globalization.CultureInfo.InvariantCulture)).Append('\n');

            try
            {
                byte[] bytes = Encoding.UTF8.GetBytes(sb.ToString());

                lock (c.writeLock) { c.stream.Write(bytes, 0, bytes.Length); c.stream.Flush(); }

            }
            catch { }
        }

        /// <summary>模块名 → 显示名 / 是否可加。</summary>
        private static readonly string[] ModuleKeys = { "basics", "atmo", "atmoVis", "terrain", "water", "rings", "clouds", "post", "orbit" };

        /// <summary>增删模块：加的模式是"从别的有该模块的星球复制一份"，保证贴图不缺失。</summary>
        private static string EditModule(Planet p, string mod, bool add)
        {
            if (p == null || p.data == null) return "no planet";
            PlanetData d = p.data;
            object cur = null;
            switch (mod)
            {
                case "basics": cur = d.basics; break;
                case "atmo": cur = d.atmospherePhysics; break;
                case "atmoVis": cur = d.atmosphereVisuals; break;
                case "terrain": cur = d.terrain; break;
                case "water": cur = d.water; break;
                case "rings": cur = d.rings; break;
                case "clouds": cur = d.frontClouds; break;
                case "post": cur = d.postProcessing; break;
                case "orbit": cur = d.orbit; break;
                default: return "unknown module";
            }

            if (!add)
            {
                if (cur == null) return "already empty";
                switch (mod)
                {
                    case "basics": return "basics 不能删";
                    case "atmo": d.atmospherePhysics = null; break;
                    case "atmoVis": d.atmosphereVisuals = null; break;
                    case "terrain": return "terrain 不能删";
                    case "water": d.water = null; break;
                    case "rings": d.rings = null; break;
                    case "clouds": d.frontClouds = null; break;
                    case "post": d.postProcessing = null; break;
                    case "orbit": return "orbit 不能删";
                }
            }
            else
            {
                if (cur != null) return "already exists";
                // 优先还原删除前存下的那份
                {
                    string stash;
                    if (_removedModules.TryGetValue(p.codeName + "/" + mod, out stash) && !string.IsNullOrEmpty(stash))
                    {
                        try
                        {
                            switch (mod)
                            {
                                case "atmo": d.atmospherePhysics = JsonWrapper.FromJson<Atmosphere_Physics>(stash); break;
                                case "atmoVis": d.atmosphereVisuals = JsonWrapper.FromJson<Atmosphere_Visuals>(stash); break;
                                case "terrain": d.terrain = JsonWrapper.FromJson<TerrainModule>(stash); break;
                                case "water": d.water = JsonWrapper.FromJson<WaterModule>(stash); break;
                                case "rings": d.rings = JsonWrapper.FromJson<RingsModule>(stash); break;
                                case "clouds": d.frontClouds = JsonWrapper.FromJson<FrontCloudsModule>(stash); break;
                                case "post": d.postProcessing = JsonWrapper.FromJson<PostProcessingModule>(stash); break;
                                case "orbit": d.orbit = JsonWrapper.FromJson<OrbitModule>(stash); break;
                                default: stash = null; break;
                            }
                            if (stash != null)
                            {
                                _removedModules.Remove(p.codeName + "/" + mod);
                                goto recompute;
                            }
                        }
                        catch (Exception e) { Log.Error("restore module: " + e.Message); }
                    }
                }
                // 从第一个拥有该模块的星球复制一份（JSON 克隆，贴图引用一并带过来）
                Planet donor = null;
                Planet[] all = EditSession.AllPlanets();
                for (int i = 0; i < all.Length; i++)
                {
                    if (all[i] == null || all[i] == p || all[i].data == null) continue;
                    object v = null;
                    switch (mod)
                    {
                        case "atmo": v = all[i].data.atmospherePhysics; break;
                        case "atmoVis": v = all[i].data.atmosphereVisuals; break;
                        case "water": v = all[i].data.water; break;
                        case "rings": v = all[i].data.rings; break;
                        case "clouds": v = all[i].data.frontClouds; break;
                        case "post": v = all[i].data.postProcessing; break;
                case "terrain": v = all[i].data.terrain; break;
                case "orbit": v = all[i].data.orbit; break;
                    }
                    if (v != null) { donor = all[i]; break; }
                }
                if (donor == null) return "没有可复制的样板星球";

                string json = null;
                switch (mod)
                {
                    case "atmo": json = JsonWrapper.ToJson(donor.data.atmospherePhysics, false); break;
                    case "atmoVis": json = JsonWrapper.ToJson(donor.data.atmosphereVisuals, false); break;
                    case "water": json = JsonWrapper.ToJson(donor.data.water, false); break;
                    case "rings": json = JsonWrapper.ToJson(donor.data.rings, false); break;
                    case "clouds": json = JsonWrapper.ToJson(donor.data.frontClouds, false); break;
                    case "post": json = JsonWrapper.ToJson(donor.data.postProcessing, false); break;
                case "terrain": json = JsonWrapper.ToJson(donor.data.terrain, false); break;
                case "orbit": json = JsonWrapper.ToJson(donor.data.orbit, false); break;
                }
                if (json == null) return "clone failed";
                switch (mod)
                {
                    case "atmo": d.atmospherePhysics = JsonWrapper.FromJson<Atmosphere_Physics>(json); break;
                    case "atmoVis": d.atmosphereVisuals = JsonWrapper.FromJson<Atmosphere_Visuals>(json); break;
                    case "water": d.water = JsonWrapper.FromJson<WaterModule>(json); break;
                    case "rings": d.rings = JsonWrapper.FromJson<RingsModule>(json); break;
                    case "clouds": d.frontClouds = JsonWrapper.FromJson<FrontCloudsModule>(json); break;
                    case "post": d.postProcessing = JsonWrapper.FromJson<PostProcessingModule>(json); break;
                case "terrain": d.terrain = JsonWrapper.FromJson<TerrainModule>(json); break;
                case "orbit": d.orbit = JsonWrapper.FromJson<OrbitModule>(json); break;
                }
            }

        recompute:
            // 重算 has* 标记
            d.hasAtmospherePhysics = d.atmospherePhysics != null;
            d.hasAtmosphereVisuals = d.atmosphereVisuals != null;
            d.hasFrontClouds = d.frontClouds != null;
            d.hasTerrain = d.terrain != null;
            d.hasWater = d.hasTerrain && d.water != null;
            d.hasRings = d.rings != null;
            d.hasPostProcessing = d.postProcessing != null;
            d.hasOrbit = d.orbit != null;
            return "ok";
        }
        /// <summary>手动构造/克隆的 PlanetData 不会走 OnDeserialized，这里把 has* 标记补算一遍。</summary>
        private static void NormalizeFlags(PlanetData d)
        {
            if (d == null) return;
            d.hasAtmospherePhysics = d.atmospherePhysics != null;
            d.hasAtmosphereVisuals = d.atmosphereVisuals != null;
            d.hasFrontClouds = d.frontClouds != null;
            d.hasTerrain = d.terrain != null;
            d.hasWater = d.hasTerrain && d.water != null;
            d.hasRings = d.rings != null;
            d.hasPostProcessing = d.postProcessing != null;
            d.hasOrbit = d.orbit != null;
            if (d.logs == null) d.logs = new LogsModule();
            if (d.landmarks == null) d.landmarks = new List<LandmarkData>();
        }
        // ---------------- 实时新建星球（不用重载世界）----------------
        private static Planet AddPlanetLive(string name, PlanetData data)
        {
            try
            {
                PlanetLoader loader = Base.planetLoader;
                if (loader == null || loader.planets == null) return null;
                if (loader.planets.ContainsKey(name)) return null;

                NormalizeFlags(data);   // 关键：补算 hasOrbit 等标记（手动构造的 data 不会走 OnDeserialized）
                try
                {
                    if (Base.worldBase != null && Base.worldBase.settings != null)
                        Base.worldBase.settings.difficulty.ScalePlanetData(data);
                }
                catch (Exception e) { Log.Error("scale new planet: " + e.Message); }

                Transform t = new GameObject(name).transform;
                t.SetParent(loader.transform, false);
                Planet p = t.gameObject.AddComponent<Planet>();
                p.SetupData(name, data, loader.terrainShader, loader.waterShader,
                            loader.atmosphereShader, loader.frontCloudsShader, loader.ringsShader,
                            new MsgNone());
                loader.planets[name] = p;

                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupInteractions(loader.planets);
                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupDepthAndSatelliteIndex();
                FieldTree.Invalidate();
                LiveApply.AttachEnvironment(p);
                LiveApply.RebuildMapForNewPlanet(p);
                try { Log.Info("planet " + name + " ssPos=" + p.GetSolarSystemPosition(0.0).magnitude.ToString("G4") + " hasOrbit=" + data.hasOrbit); } catch { }
                Log.Info("planet added live: " + name);
                return p;
            }
            catch (Exception e)
            {
                Log.Error("add planet live: " + e);
                return null;
            }
        }

        // 被删掉的模块先存一份 JSON，重新启用时原样还原
        private static readonly Dictionary<string, string> _removedModules = new Dictionary<string, string>();
        // ---------------- 改名 / 删除星球 ----------------
        private static string RenamePlanet(Planet p, string newName)
        {
            if (p == null) return "no planet";
            if (string.IsNullOrEmpty(newName)) return "empty name";
            newName = newName.Trim().Replace(" ", "_");
            foreach (char ch in System.IO.Path.GetInvalidFileNameChars())
                newName = newName.Replace(ch, '_');
            if (newName.Length == 0) return "bad name";

            PlanetLoader loader = Base.planetLoader;
            if (loader == null || loader.planets == null) return "no loader";
            if (loader.planets.ContainsKey(newName) && newName != p.codeName) return "名字已存在";

            string oldName = p.codeName;
            if (oldName == newName) return "ok";

            IFolder pack = PackIO.EnsurePack();
            if (pack == null) return "no pack";
            IFolder pd = pack.GetFolder("Planet Data");

            // 1) 文件改名
            try
            {
                IFile oldFile = pd.GetFile(oldName + ".txt");
                if (oldFile.Exists())
                {
                    oldFile.Move(pd.GetFile(newName + ".txt"));
                }
            }
            catch (Exception e) { Log.Error("rename file: " + e.Message); }

            // 2) 其它星球的 orbit.parent 跟着改
            foreach (Planet q in loader.planets.Values)
            {
                if (q == null || q.data == null || q.data.orbit == null) continue;
                if (q.data.orbit.parent == oldName) q.data.orbit.parent = newName;
            }

            // 3) 内存里改名
            loader.planets.Remove(oldName);
            p.codeName = newName;
            p.gameObject.name = newName;
            loader.planets[newName] = p;
            try
            {
                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupInteractions(loader.planets);
                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupDepthAndSatelliteIndex();
            }
            catch (Exception e) { Log.Error("rename relink: " + e.Message); }

            FieldTree.Invalidate();
            Log.Info("renamed " + oldName + " -> " + newName);
            return "ok";
        }

        private static string DeletePlanet(Planet p)
        {
            if (p == null) return "no planet";
            PlanetLoader loader = Base.planetLoader;
            if (loader == null || loader.planets == null) return "no loader";
            if (p.codeName == "Sun") return "太阳不能删";
            if (p.satellites != null && p.satellites.Length > 0) return "它还有 " + p.satellites.Length + " 颗卫星，先处理卫星";
            if (loader.planets.Count <= 1) return "至少要留一颗星球";

            // 玩家在它上面 / 空间中心在它上面
            try
            {
                if (PlayerController.main != null && PlayerController.main.player.Value != null)
                {
                    SFS.World.WorldLocation loc = PlayerController.main.player.Value.location;
                    if (loc != null && loc.planet == p) return "玩家正在这颗星球上";
                }
            }
            catch { }
            try
            {
                if (loader.spaceCenter != null && loader.spaceCenter.address == p.codeName) return "这里是发射场所在的星球";
            }
            catch { }

            string name = p.codeName;

            // 文件
            try
            {
                IFolder pack = PackIO.EnsurePack();
                if (pack != null)
                {
                    IFile file = pack.GetFolder("Planet Data").GetFile(name + ".txt");
                    if (file.Exists()) file.Delete();
                }
            }
            catch (Exception e) { Log.Error("delete file: " + e.Message); }

            // 内存
            try { LiveApply.DetachEnvironment(p); } catch (Exception e) { Log.Error("detach env: " + e.Message); }
            try { LiveApply.DetachMapEnvironment(p); } catch { }
            loader.planets.Remove(name);
            try { UnityEngine.Object.Destroy(p.gameObject); } catch { }
            try
            {
                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupInteractions(loader.planets);
                foreach (Planet q in new List<Planet>(loader.planets.Values)) q.SetupDepthAndSatelliteIndex();
            }
            catch (Exception e) { Log.Error("delete relink: " + e.Message); }

            FieldTree.Invalidate();
            Log.Info("deleted planet " + name);
            // 删除后不做地图重建：节点已在 DetachMapEnvironment 里隐藏，重建会踩到 LineRenderer 池
            return "ok";
        }
        /// <summary>只删文件（不动运行时对象，避免 Environment 空引用）。</summary>
        private static string DeletePlanetFile(Planet p)
        {
            if (p == null) return "no planet";
            PlanetLoader loader = Base.planetLoader;
            if (loader == null || loader.planets == null) return "no loader";
            if (p.codeName == "Sun") return "太阳不能删";
            if (p.satellites != null && p.satellites.Length > 0) return "它还有 " + p.satellites.Length + " 颗卫星";
            if (loader.planets.Count <= 1) return "至少要留一颗星球";
            if (loader.spaceCenter != null && loader.spaceCenter.address == p.codeName) return "这里是发射场所在的星球";
            try
            {
                if (PlayerController.main != null && PlayerController.main.player.Value != null)
                {
                    SFS.World.WorldLocation loc = PlayerController.main.player.Value.location;
                    if (loc != null && loc.planet == p) return "玩家正在这颗星球上";
                }
            }
            catch { }

            string name = p.codeName;
            try
            {
                IFolder pack = PackIO.EnsurePack();
                if (pack != null)
                {
                    IFile file = pack.GetFolder("Planet Data").GetFile(name + ".txt");
                    if (file.Exists()) file.Delete();
                }
            }
            catch (Exception e) { Log.Error("delete file: " + e.Message); }

            Log.Info("deleted planet file " + name + "（重载世界后消失）");
            return "ok";
        }
        /// <summary>取目标星球：优先命令行参数，其次当前选中的。</summary>
        private static Planet ResolveTarget(string[] parts)

        {
            if (parts.Length > 2)

            {
                Planet p = FindPlanet(parts[2]);

                if (p != null) return p;

            }
            return EditSession.Target;

        }
        private static void Execute(Client c, string line)

        {
            if (string.IsNullOrEmpty(line)) return;

            string[] parts = line.Split(new[] { ' ' }, 3, StringSplitOptions.RemoveEmptyEntries);

            string cmd = parts.Length > 0 ? parts[0].ToUpperInvariant() : "";


            switch (cmd)
            {
                case "PING":

                    Send(c, "PONG " + Application.version);

                    return;


                case "STATUS":
                    Send(c, "STATUS world=" + (EditSession.Active ? "edit" : "idle") +

                            " target=" + (EditSession.Target != null ? EditSession.Target.codeName : "-") +

                            " paint=" + (TerrainBrush.Enabled ? 1 : 0) +

                            " brush=" + (int)TerrainBrush.RadiusMeters +

                            " samples=" + TerrainBrush.SampleCount + " shape=" + TerrainBrush.Shape +

                            " rate=" + (int)TerrainBrush.RateMeters);

                    return;


                case "LIST":
                    {
                        Planet[] all = EditSession.AllPlanets();

                        var lines = new List<string>(all.Length);

                        for (int i = 0; i < all.Length; i++)

                        {
                            Planet p = all[i];

                            if (p == null) continue;

                            lines.Add(p.codeName + "\t" + (int)p.Radius + "\t" + p.data.basics.gravity.ToString("G6"));

                        }
                        SendMany(c, "PLANETS " + lines.Count, lines);

                        return;

                    }

                case "FIELDS":
                    {
                        if (parts.Length < 2) { Send(c, "ERR missing planet"); return; }

                        Planet p = FindPlanet(parts[1]);

                        if (p == null) { Send(c, "ERR no such planet"); return; }

                        EditSession.SelectPlanet(p);

                        Ui.Rebuild();
                        List<FieldEntry> entries = FieldTree.For(p);

                        var lines = new List<string>(entries.Count);

                        for (int i = 0; i < entries.Count; i++)

                        {
                            FieldEntry e = entries[i];

                            lines.Add(((int)e.Kind) + "\t" + e.Key + "\t" + e.GetText());

                        }
                        SendMany(c, "FIELDS " + p.codeName + " " + lines.Count, lines);

                        return;

                    }

                case "PAINTAT":

                    {
                        // PAINTAT <planet> <angleRad> <deltaMeters>   —— 画笔式：沿轨迹加减高度

                        if (parts.Length < 3) { Send(c, "ERR usage"); return; }

                        Planet p = FindPlanet(parts[1]);

                        if (p == null) { Send(c, "ERR no such planet"); return; }

                        string[] nums = parts[2].Split(' ');

                        double ang, delta;

                        if (nums.Length < 2 ||

                            !double.TryParse(nums[0], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out ang) ||

                            !double.TryParse(nums[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out delta))

                        { Send(c, "ERR bad numbers"); return; }


                        bool was = TerrainBrush.Enabled;

                        TerrainBrush.Enabled = true;

                        TerrainBrush.Ensure(p);

                        TerrainBrush.MaybeBeginStroke();

                        TerrainBrush.Paint(p, ang, (float)delta);

                        TerrainBrush.ApplyNow(p);

                        TerrainBrush.Enabled = was;

                        return;

                    }
                case "PAINTTO":

                    {
                        // PAINTTO <planet> <angleRad> <targetHeight> <factor>

                        if (parts.Length < 3) { Send(c, "ERR usage"); return; }

                        Planet p = FindPlanet(parts[1]);

                        if (p == null) { Send(c, "ERR no such planet"); return; }

                        string rest = parts[2];

                        string[] nums = rest.Split(' ');

                        double ang, tgt;

                        float fac = 0.35f;
                        if (nums.Length < 2 ||

                            !double.TryParse(nums[0], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out ang) ||

                            !double.TryParse(nums[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out tgt))

                        { Send(c, "ERR bad numbers"); return; }

                        if (nums.Length > 2) float.TryParse(nums[2], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out fac);


                        bool wasDirty = TerrainBrush.Enabled;

                        TerrainBrush.Enabled = true;

                        TerrainBrush.Ensure(p);

                        TerrainBrush.MaybeBeginStroke();

                        TerrainBrush.PaintTo(p, ang, tgt, fac);

                        TerrainBrush.ApplyNow(p);

                        TerrainBrush.Enabled = wasDirty;

                        return;      // 形状由调用方随后 SHAPE 拉取，或这里直接回

                    }
                case "SHAPE":
                    {
                        // SHAPE <planet>  →  SHAPE <code> <n> <radius> <r> <g> <b> + n 行高度
                        if (parts.Length < 2) { Send(c, "ERR missing planet"); return; }
                        Planet p = FindPlanet(parts[1]);
                        if (p == null) { Send(c, "ERR no such planet"); return; }
                        SendShape(c, p, 180);
                        return;
                    }

                case "SET":
                    {
                        // SET <planet> <path> <value>

                        if (parts.Length < 3) { Send(c, "ERR usage: SET <planet> <path> <value>"); return; }

                        string rest = parts[2];

                        int sp = rest.IndexOf(' ');

                        if (sp < 0) { Send(c, "ERR missing value"); return; }

                        string path = rest.Substring(0, sp).Trim();

                        string value = rest.Substring(sp + 1);


                        Planet p = FindPlanet(parts[1]);

                        if (p == null) { Send(c, "ERR no such planet"); return; }

                        List<FieldEntry> entries = FieldTree.For(p);

                        FieldEntry hit = null;

                        for (int i = 0; i < entries.Count; i++)

                        {
                            if (entries[i].Key == path) { hit = entries[i]; break; }

                        }
                        if (hit == null) { Send(c, "ERR no such field"); return; }

                        if (!hit.TrySetText(value)) { Send(c, "ERR bad value"); return; }

                        // 关键：立刻把改动应用到游戏世界，并排队写回星球文件
                        EditSession.ApplyNow(false);
                        EditSession.MarkDirty();   // 放在 apply 之后，避免被清掉
                        Log.Info("set " + p.codeName + " " + path + " = " + hit.GetText());
                        Ui.Rebuild();
                        Send(c, "OK " + path + " = " + hit.GetText());

                        return;

                    }

                case "CMD":
                    {
                        string name = parts.Length > 1 ? parts[1].ToLowerInvariant() : "";

                        switch (name)

                        {
                            case "apply":
                                EditSession.ApplyNow(false);

                                Send(c, "OK apply");

                                return;

                            case "save":
                                EditSession.ApplyNow(true);

                                Send(c, "OK save");

                                return;

                            case "reload":
                                EditSession.ReloadFromFile();

                                Send(c, "OK reload");

                                return;

                            case "paint":

                                TerrainBrush.Enabled = !TerrainBrush.Enabled;

                                if (TerrainBrush.Enabled) TerrainBrush.Ensure(EditSession.Target);

                                Send(c, "OK paint=" + (TerrainBrush.Enabled ? 1 : 0));

                                return;

                            case "focus":
                                {
                                    if (parts.Length < 3) { Send(c, "ERR usage: CMD focus <planet>"); return; }

                                    Planet fp = FindPlanet(parts[2]);

                                    if (fp == null) { Send(c, "ERR no such planet"); return; }

                                    EditSession.SelectPlanet(fp);

                                    Send(c, "OK focus " + fp.codeName + " edit=" + (EditSession.Active ? 1 : 0));

                                    return;

                                }
                            case "ui":
                                EditSession.HideInGameUi = (parts.Length > 2 && parts[2] == "off");

                                Send(c, "OK ui=" + (EditSession.HideInGameUi ? "off" : "on"));

                                return;

                            case "samples":
                                {
                                    int n;

                                    if (parts.Length < 3 || !int.TryParse(parts[2], out n)) { Send(c, "ERR samples value"); return; }

                                    TerrainBrush.SetSampleCount(EditSession.Target, n);

                                    try { PackIO.SaveBrush(PackIO.EnsurePack()); } catch { }

                                    Send(c, "OK samples=" + TerrainBrush.SampleCount);

                                    return;

                                }
                            case "rate":
                                {
                                    float v;
                                    if (parts.Length < 3 || !float.TryParse(parts[2], out v)) { Send(c, "ERR rate value"); return; }

                                    TerrainBrush.RateMeters = Mathf.Clamp(v, 10f, 5000f);

                                    Send(c, "OK rate=" + (int)TerrainBrush.RateMeters);

                                    return;

                                }
                            case "shape":
                                {
                                    int n;

                                    if (parts.Length < 3 || !int.TryParse(parts[2], out n)) { Send(c, "ERR shape value"); return; }

                                    TerrainBrush.Shape = Mathf.Clamp(n, 0, 2);

                                    Send(c, "OK shape=" + TerrainBrush.Shape);

                                    return;

                                }
                            case "addmod":
                            case "delmod":
                                {
                                    // 命令被 Split(3) 切成 3 段，这里再拆一次：<planet> <module>
                                    string tail = parts.Length > 2 ? parts[2].Trim() : "";
                                    string[] sub = tail.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                                    if (sub.Length < 2) { Send(c, "ERR usage: CMD addmod|delmod <planet> <module>"); return; }
                                    Planet pm = FindPlanet(sub[0]);
                                    if (pm == null) pm = EditSession.Target;
                                    string modName = sub[1];   // 保持大小写（atmoVis 这种是驼峰）
                                    string res = EditModule(pm, modName, name == "addmod");
                                    if (res == "ok")
                                    {
                                        FieldTree.Invalidate();
                                        LiveApply.SyncMapEnvironment(pm);
                                        EditSession.ApplyNow(false);
                                        EditSession.MarkDirty();   // 排进写盘队列
                                        Send(c, "OK " + name + " " + modName);
                                    }
                                    else Send(c, "ERR " + res);
                                    return;
                                }
                            case "newplanet":
                                {
                                    if (parts.Length < 3) { Send(c, "ERR usage: CMD newplanet <earth|rocky|gas|ice|asteroid|bary>"); return; }
                                    // 命令被 Split(3) 切成 3 段，这里再拆：<key> [live]
                                    string[] subN = parts[2].Trim().Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                                    string key = subN.Length > 0 ? subN[0].ToLowerInvariant() : "";
                                    bool wantLive = subN.Length > 1 && subN[1].ToLowerInvariant() == "live";
                                    string tpl = null;
                                    if (key == "earth") tpl = "Earth";
                                    else if (key == "rocky") tpl = "Mars";
                                    else if (key == "gas") tpl = "Jupiter";
                                    else if (key == "ice") tpl = "Europa";
                                    else if (key == "asteroid") tpl = "Phobos";
                                    else if (key == "bary") tpl = "";
                                    else { Send(c, "ERR unknown template"); return; }

                                    Planet src = string.IsNullOrEmpty(tpl) ? null : FindPlanet(tpl);
                                    if (!string.IsNullOrEmpty(tpl) && src == null) { Send(c, "ERR template planet not found: " + tpl); return; }

                                    string nn;
                                    PlanetData npData;
                                    string res = CreatePlanetFromTemplate(tpl, out nn, out npData);
                                    if (res == "ok")
                                    {
                                        // 默认只写文件（实时注册会让游戏在 Environment 未建好时抛空引用）
                                        // 默认只写文件；只有显式 live 才实时注册（已补建 Environment）
                                        if (wantLive)
                                        {
                                            Planet np = AddPlanetLive(nn, npData);
                                            Send(c, "OK newplanet " + nn + (np != null ? " live" : " (reload)"));
                                        }
                                        else
                                        {
                                            Send(c, "OK newplanet " + nn + " (reload)");
                                        }
                                    }
                                    else Send(c, "ERR " + res);
                                    return;
                                }
                            case "shapefine":
                                {
                                    string tailS = parts.Length > 2 ? parts[2].Trim() : "";
                                    string[] subS = tailS.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                                    Planet pf = subS.Length > 0 ? FindPlanet(subS[0]) : null;
                                    if (pf == null) pf = EditSession.Target;
                                    if (pf == null) { Send(c, "ERR no planet"); return; }
                                    int nn = 4096;
                                    if (subS.Length > 1) int.TryParse(subS[1], out nn);
                                    SendShape(c, pf, nn);
                                    return;
                                }
                            case "soifit":
                                {
                                    Planet ps = ResolveTarget(parts);
                                    if (ps == null) { Send(c, "ERR no planet"); return; }
                                    string res = FitSOI(ps);
                                    if (res.StartsWith("ok") || res.StartsWith("already"))
                                    {
                                        EditSession.ApplyNow(false);
                                        EditSession.MarkDirty();
                                        Send(c, "OK " + res);
                                    }
                                    else Send(c, "ERR " + res);
                                    return;
                                }
                            case "rename":
                                {
                                    // CMD rename <old> <new>（命令被 Split(3) 切成 3 段，这里再拆）
                                    string tail3 = parts.Length > 2 ? parts[2].Trim() : "";
                                    string[] sub3 = tail3.Split(new[] { ' ' }, 2);
                                    Planet pr = sub3.Length > 0 ? FindPlanet(sub3[0]) : null;
                                    if (pr == null) pr = EditSession.Target;
                                    if (pr == null) { Send(c, "ERR no planet"); return; }
                                    string newName = sub3.Length > 1 ? sub3[1].Trim() : "";
                                    string res = RenamePlanet(pr, newName);
                                    Send(c, res == "ok" ? ("OK renamed " + newName) : ("ERR " + res));
                                    return;
                                }
                            case "delplanet":
                                {
                                    // CMD delplanet <name> [live]
                                    string[] subD = (parts.Length > 2 ? parts[2].Trim() : "").Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                                    string delName = subD.Length > 0 ? subD[0] : "";
                                    bool delLive = subD.Length > 1 && subD[1].ToLowerInvariant() == "live";
                                    Planet pd2 = FindPlanet(delName);
                                    if (pd2 == null) pd2 = EditSession.Target;
                                    if (pd2 == null) { Send(c, "ERR no planet"); return; }
                                    string res = delLive ? DeletePlanet(pd2) : DeletePlanetFile(pd2);
                                    Send(c, res == "ok" ? "OK deleted" : ("ERR " + res));
                                    return;
                                }
                            case "clear":
                                {
                                    Planet pc = ResolveTarget(parts);

                                    bool okC = TerrainBrush.ClearAll(pc);

                                    if (okC)
                                    {
                                        try { PackIO.SaveBrush(PackIO.EnsurePack()); } catch { }


                                    }
                                    Send(c, okC ? "OK clear" : "ERR clear failed");
                                    if (pc != null) SendShape(c, pc);

                                    return;

                                }
                            case "redo":
                                {
                                    Planet pr = ResolveTarget(parts);
                                    bool okR = TerrainBrush.Redo(pr);

                                    if (okR) { try { PackIO.SaveBrush(PackIO.EnsurePack()); } catch { } }
                                    Send(c, okR ? "OK redo" : "ERR nothing to redo");
                                    if (okR && pr != null) SendShape(c, pr);

                                    return;

                                }
                            case "undo":

                                {
                                    Planet pu = ResolveTarget(parts);

                                    bool okU = TerrainBrush.Undo(pu);


                                    if (okU) { try { PackIO.SaveBrush(PackIO.EnsurePack()); } catch { } }
                                    Send(c, okU ? "OK undo" : "ERR nothing to undo");
                                    if (okU && pu != null) SendShape(c, pu);

                                    return;

                                }
                            case "brush":
                                {
                                    float v;
                                    if (parts.Length < 3 || !float.TryParse(parts[2], out v)) { Send(c, "ERR brush value"); return; }

                                    TerrainBrush.RadiusMeters = Mathf.Clamp(v, 10f, 20000f);

                                    Send(c, "OK brush=" + (int)TerrainBrush.RadiusMeters);

                                    return;

                                }
                            default:
                                Send(c, "ERR unknown cmd");

                                return;

                        }
                    }

                default:
                    Send(c, "ERR unknown command");

                    return;

            }
        }

        private static Planet FindPlanet(string codeName)

        {
            Planet[] all = EditSession.AllPlanets();

            for (int i = 0; i < all.Length; i++)

            {
                if (all[i] != null && string.Equals(all[i].codeName, codeName, StringComparison.OrdinalIgnoreCase))

                    return all[i];

            }
            return null;

        }
    }
}
