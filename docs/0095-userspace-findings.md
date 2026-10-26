# 0095 runtime findings

Read-only recovery collection completed on 2026-10-26. The native archive
receipt reports complete=true, errors=[], header_unchanged=true and 1193 files.
Only boot 649b5828e098417b8e2d78878287efa7 was decoded: 136953 events.
Kernel epoch56 contains 23 missing positions and 25 producer drops. Recovery
export does not replay the HFS journal, so the captured dirty filesystem is
reported as observed. Raw logs and device metadata remain private under out/.

## HFS protection fix progressed the migration

PosterBoard now reaches versions59,60,61 and logs:
`Not cleaning up after successful data store migration.`
There are no captured instances of the old `Failed to migrate` or
`could not initialize data store` messages in this boot. This is evidence for
migration progress, not a working wallpaper UI.

## New explicit PosterBoard abort

PosterBoard repeatedly raises NSInvalidArgumentException:
`WKWallpaperRepresentingCollectionsManager: Collections.plist ... does not
contain valid value for keypath: order`.
The requested path is /Library/Wallpaper/Collections/Collections.plist.
The original source filesystem instead supplies Collections~iphone.plist,
whose order array contains ten identifiers. No generic Collections.plist is
present in the original image. Native logs also say there are no wallpaper
bundles they want to load. Do not replace the original collection list with
an invented generic plist: correct device-resource selection first.

## Old MobileGestalt identity remains cached

The current device cache was read through the HFS catalog with hard-link
resolution. Its CacheVersion is23G90, CacheExtra contains36 entries, and the
artwork dictionary oPeik/9e8lQWMszEjbPzng contains only
ArtworkDeviceScaleFactor=3. Five values still describe iPod/iPod touch and five
retain the previous Samsung software persona. These are obsolete cached
answers despite the current loader product metadata.

CoreUI still reports missing artwork idiom, gamut and feature-set class, six
messages for each. Stale MobileGestalt cache is a strong explanation for
incorrect device-specific resource selection; regeneration and fresh runtime
answers are required to prove the complete chain.

## Application bootstrap published but did not run

Kernel publication error=0. Resolving the HFS hard links verifies both72,480-byte
executable and725-byte job; the executable matches the0095 built SHA256.
There is no captured helper process, helper output log, lock or attempt receipt.
Both installed-bundle roots still contain zero installed .app directories.

The job was published under /Library/LaunchDaemons. The shipped launchd embedded
Bootstrap.Paths contains /System/Library and /AppleInternal/Library launch
locations, not /Library/LaunchDaemons. Its cache loader log confirms that the
original /System/Library/xpc/launchd.plist was successfully supplied. The owned
job is not in that cache. Publication alone therefore did not register it with
launchd. A corrected supported registration route must preserve existing jobs
and the previously approved one-shot migration scope.

## Remaining visible failures

SpringBoard still exits with Invalid condition not satisfying:
_bs_assert_object != nil, namespace2/code5. The native log contains39 null image
colorspace warnings and328 invalid-context warnings. These counts do not prove
that the drawing failure is solely caused by the stale resource profile.

No new BOOT was built or flashed during this capture. 0095 remains the installed
checkpoint. Required next fixes are native MobileGestalt cache regeneration and
correct registration of the approved migration job, then runtime verification
of app installation, poster configuration and Settings behavior.
