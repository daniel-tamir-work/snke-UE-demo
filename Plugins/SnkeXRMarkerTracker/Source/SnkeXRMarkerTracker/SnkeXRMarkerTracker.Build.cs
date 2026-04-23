using UnrealBuildTool;
using System.IO;

public class SnkeXRMarkerTracker : ModuleRules
{
	public SnkeXRMarkerTracker(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// URecenterSysPropComponent -> UHeadMountedDisplayFunctionLibrary (XRBase in UE 5.4)
			"HeadMountedDisplay",
			"XRBase",
			// UGraspGrabComponent -> EControllerHand
			"InputCore",
		});

		// Cobra SDK headers (bundled in plugin)
		string CobraSDKInclude = Path.Combine(PluginDirectory, "Source", "ThirdParty", "CobraSDK", "include");
		PublicIncludePaths.Add(CobraSDKInclude);

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			string LibDir = Path.Combine(PluginDirectory, "Lib", "Android", "arm64-v8a");
			PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libmarker_tracker_client.so"));

			AdditionalPropertiesForReceipt.Add("AndroidPlugin",
				Path.Combine(PluginDirectory, "SnkeXRMarkerTracker_UPL.xml"));
		}
	}
}
