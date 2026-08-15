# Mission

meta-quest3-lab is a native C++ XR laboratory for Meta Quest 3 and Quest 3S, built to
learn spatial computing from first principles instead of from an engine.

*"Draw only what you have measured."*

Every application in `apps/` is repository-owned code that talks directly to OpenXR and
Vulkan through the Android NDK. There is no Unity, no Unreal, and no vendor abstraction
layer standing between the code and the runtime. Meta-specific capabilities — passthrough,
spatial anchors, the passthrough camera, environment depth — are isolated behind small
interfaces in `libs/` so the portable path stays visible and the vendor path stays
replaceable.

The lab progresses as a milestone ladder. It starts with the smallest correct OpenXR
lifecycle, adds stereo Vulkan rendering, then poses and reference spaces, controller and
hand interaction, passthrough, and anchors. From there it turns toward the real goal:
integrating a real-time computer-vision pipeline — RF-DETR object detection running on
device or streamed to a host — with a spatially correct XR interface.

The project's governing discipline is that a rendered claim must be a measured claim. A
monocular detection yields a bearing and nothing else, so app 10 renders bearing rays and
refuses to draw a 3D box. A billboard pinned at an operator-set distance was implemented,
recognized as a 2D overlay wearing 3D clothing, and deleted rather than refined. The first
3D box waits for Milestone 14, when range, extents, and orientation are all measured.
Capability probes report honest debt: Milestone 16 measured a viable concurrent stereo
transport and recorded, in writing, that no certified optical sync relationship exists yet.

Each application stays small, independently buildable, and documented — what it
demonstrates, which OpenXR extensions it needs, how to build and deploy it, what it should
look like on the headset, and where it falls short.
