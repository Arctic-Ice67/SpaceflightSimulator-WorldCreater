What Is This?  
  
This is an “in-game planet editor” that lets you change any planet values / textures / formulas in real time.  
    
Structure   
  
It consists of two independent parts, decoupled as processes communicating over local TCP.  
   
Features   
  
· True real-time editing  
· Dual-process decoupled architecture  
· Automatic restoration of difficulty scaling  
· Liquid glass UI  
· Add/remove planets / modules at runtime  
· Safe memory / map management  
  
Source Code  
  
· Main.cs — Mod entry point   
· Bridge.cs — Key communication layer  
· Patches.cs — Takes over the camera and locks time  
· EditSession.cs — Edit mode state machine  
· FieldTree.cs — Reflection-driven editable fields  
· LiveApply.cs — Real-time rebuilding  
· TerrainBrush.cs — Terrain brush  
· PackIO.cs — Writing to disk  
  
The external panel will not be described here.  
  
## 这是什么
这是一个「游戏内星球编辑器」 可以让你可以实时改变星球任意数值 / 贴图 / 公式
## 结构
两个独立的部分 通过本地 TCP 通信解耦的进程组成
| 组件 | 语言 / 技术 | 作用 |
|------|-----------|------|
| `PlanetEditor`（DLL mod） | C# / **net472** / **Harmony** | 注入游戏进程，负责**反射改星球、重建地形、写盘** |
| `PlanetEditorUI`（独立程序） | C++20 / **ImGui** / **D3D11** | 玩家看到的**外部控制面板**（星球列表、字段表、地形预览、笔刷） |
## 特色  
真·实时编辑  
双进程解耦架构  
难度缩放自动还原  
液态玻璃 UI  
运行时增删星球 / 模块  
安全的内存 / 地图管理  
## 源码  
`Main.cs`-模组入口  
`Bridge.cs`-关键通信层  
`Patches.cs`-接管视角并锁定时间  
`EditSession.cs`-编辑模式状态机  
`FieldTree.cs`-反射驱动的可编辑字段  
`LiveApply.cs`- 实时重建  
`TerrainBrush.cs`-地形笔刷  
`PackIO.cs`- 写盘  
外部面版不再介绍  
