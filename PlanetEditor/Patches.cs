using HarmonyLib;
using SFS.World;

namespace PlanetEditor
{
    /// <summary>编辑模式下接管相机 / 锁时间加速。</summary>
    internal static class Patches
    {
        [HarmonyPatch(typeof(PlayerController), "TrackPlayer")]
        internal static class Patch_TrackPlayer
        {
            private static bool Prefix()
            {
                return !EditSession.Active;
            }
        }

        [HarmonyPatch(typeof(PlayerController), "OnZoom")]
        internal static class Patch_OnZoom
        {
            private static bool Prefix()
            {
                return !EditSession.Active;
            }
        }

        [HarmonyPatch(typeof(WorldTime), "AccelerateTime")]
        internal static class Patch_Accelerate
        {
            private static bool Prefix()
            {
                return !EditSession.Active;
            }
        }

        [HarmonyPatch(typeof(WorldTime), "DecelerateTime")]
        internal static class Patch_Decelerate
        {
            private static bool Prefix()
            {
                return !EditSession.Active;
            }
        }

        [HarmonyPatch(typeof(WorldTime), "StopTimewarp")]
        internal static class Patch_StopTimewarp
        {
            private static void Postfix()
            {
                // 编辑模式里任何"停时间加速"的路径都归一到 1x
                if (!EditSession.Active) return;
                try
                {
                    if (WorldTime.main != null && WorldTime.main.timewarpIndex != 0)
                        WorldTime.main.SetTimewarpIndex_ForLoad(0);
                }
                catch { }
            }
        }
    }
}
