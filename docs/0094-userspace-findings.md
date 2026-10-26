# 0094 recovery findings: staged applications, wallpaper and SpringBoard

Recovery collection is read-only. The native archive export reports complete,
no errors, and an unchanged HFS volume header. Kernel epoch55 has31 missing
positions and31 producer drops. Current-boot decoding selected UUID
059B0A4D42564A99B6DD4C66BD5A66F4 and produced126,494 events. Private raw evidence
is under ignored out/logs/0094-recovery and out/research-0094.

## Application payload versus installation

The live volume contains all50 top-level bundles under
/private/var/staged_system_apps and all50 corresponding AppPlaceholders.
All18,627 staged catalog entries exist at their original paths; regular-file
lengths match the prepared IPSW manifest. All50 root Info.plist contents match
its SHA256 hashes. This is not a hash verification of every executable/resource.

Neither /private/var/containers/Bundle/Application nor
/private/var/mobile/Containers/Bundle/Application contains an installed .app
bundle in this snapshot. This distinguishes the copied staging payload from a
completed installation of the user-facing system applications. Settings and
Phone already residing in /Applications do not establish completion of that
staged installation phase.

The installd file log's current pid277 section reports reboot detection and
MIIsBuildUpgrade skipping upgrade because both recorded and current versions
are23G90. The phase that should install the staged bundles still needs tracing;
that version message alone does not establish its cause. appplaceholdersyncd
is running, but its launch events include replica/device/hidden-app changes;
its presence alone is not proof that installd installed these50 bundles.

## Wallpaper data and store initialization

All1,440 prepared /Library/Wallpaper catalog paths exist. All288 regular-file
lengths match after resolving169 HFS hard links to their private iNode backing
files. Those links initially have zero-length inline forks and must not be
reported as truncated files. Three resolved samples, including one HEIC image,
match their original SHA256 hashes. This does not hash every wallpaper file.

The current native log repeatedly reports PosterBoard failing to migrate the
data store from version59 to61, followed by 'could not initialize data store'.
The same sequence also logs a nominal successful setup; that line is not a
successful wallpaper configuration, because the subsequent client connection
still receives the initialization error. The failing operation's underlying
error is not exposed in this capture. Do not delete the store or fabricate a
schema-version marker based only on the version-level message.

## Captured SpringBoard failure

Kernel pid28 records exception6, reason namespace2/code5 and:
`Invalid condition not satisfying: _bs_assert_object != nil`.
The corresponding native failure is in PosterBoardUIServices:
`PRUISModalEntryPointEditHomeScreen`
`initWithServiceConfiguration:transitionOverlayRenderId:transitionOverlayContextId:`
(PRUISModalEntryPoint.m:637). In the pristine shipped implementation, x2 is
retained as the service configuration and a null value branches directly into
the assertion path before accessing PRSPosterConfiguration. Thus a missing
wallpaper service configuration is demonstrated for this captured crash.
This does not prove that every Settings tap reaches this exact action.

SpringBoard is relaunched afterward. Native rendering still logs46 NULL-image-
colorspace errors and328 invalid-context messages. The prior CoreImage
'destination too big' / 'Failed to render' messages are absent in this capture.

## Missing artwork device metadata

CoreUI explicitly reports missing artwork idiom, gamut and graphics feature
class; the returned artwork dictionary contains only scale3. The local pristine
libMobileGestalt implementation reads product-description, artwork-device-idiom,
artwork-device-subtype, artwork-scale-factor, artwork-dynamic-displaymode,
device-perf-memory-class, graphics-featureset-class and fallbacks from
IODeviceTree:/product. Its gamut query reads artwork-display-gamut there.
Our generated /product currently contains only name, product-name, model and
board-config. This is a concrete incomplete platform contract. It is not a
reason to claim an Apple Metal feature set on the CPU-only display path.

The evidence supports three distinct remaining tasks: complete native staged
application installation; identify and repair PosterBoard store initialization;
and supply verified artwork/display metadata and trace the remaining NULL
colorspace path. This investigation did not install a new image or establish
that those issues are fixed.
