# Mikage changes

This is Mikage's fork of [krkrsdl3](https://github.com/krkrsdl3/krkrsdl3), used by the Mikage iOS player.

- Upstream code keeps its own license: see [LICENSE](LICENSE). Mikage doesn't relicense it.
- Code written for Mikage is licensed under the GNU General Public License, version 3 or (at your option) any later version (`GPL-3.0-or-later`):
  - files whose first line is `// SPDX-License-Identifier: GPL-3.0-or-later`;
  - blocks inside upstream files that begin with a `// Mikage (GPL-3.0-or-later):` comment.

Files written for Mikage:

- `core/media/image/ImagePrefetchPolicy.h`
- `core/render/AsyncLayerReadback.h`
- `core/render/CPUConsumerTrace.h`
- `core/render/CPUFrameDiagnostics.h`
- `core/render/CPUReadAggregation.h`
- `core/render/LayerAffineGeometry.h`
- `core/render/LayerHotspotContext.h`
- `core/render/LayerImageDiagnostics.h`
- `core/render/LayerOperationDefinitions.def`
- `core/render/LayerOperationShaderDefinitions.h`
- `core/render/LayerPerspectiveGeometry.h`
- `core/render/LayerRenderOperation.h`
- `core/render/LayerShrink.h`
- `core/render/LayerShrinkDefinitions.def`
- `core/render/LayerShrinkGeometry.h`
- `core/render/LayerSpanComposite.h`
- `core/render/LayerSpanCompositeDefinitions.def`
- `core/render/LayerSpanCompositeGeometry.h`
- `core/render/LayerSpanRouteDiagnostics.h`
- `core/render/LayerTransition.h`
- `core/render/LayerTransitionDefinitions.def`
- `core/render/LayerTransitionGeometry.h`
- `core/render/LayerTriangleTrace.h`
- `core/render/LayerUploadBatch.h`
- `core/render/LayerWorkDiagnostics.h`
- `core/render/MetalLayerRenderManager.cpp`
- `core/render/MetalLayerRenderManager.h`
- `core/render/PointReadTrace.h`
- `core/render/PrimaryCompletion.h`
- `core/render/UpdateRegionCoalescing.h`
- `core/render/backend/LayerHotspotDiagnostics.h`
- `core/render/backend/LayerInitialization.h`
- `core/render/backend/MetalLayerShaders.h`
- `core/render/backend/MetalRenderBackend.h`
- `core/render/backend/MetalRenderBackend.mm`
- `core/render/backend/MetalShrinkShaders.h`
- `core/render/backend/MetalSpanCompositeShaders.h`
- `core/render/backend/MetalStageDiagnostics.h`
- `core/render/backend/MetalTransitionShaders.h`
- `core/render/backend/OrdinaryLayerPipeline.h`
- `core/render/backend/PendingLayerFill.h`
- `core/render/backend/StaticPresentation.h`
- `core/render/backend/TransitionCache.h`
- `core/script/TVPSkippedMovies.cpp`
- `core/script/TVPSkippedMovies.h`
- `plugins/AMVDecodedFrameCache.h`
- `plugins/LayerExDraw/SpanCapture.h`
- `plugins/ScopedLayerPixels.h`
- `plugins/emoteplayer/emoteperformance.h`
- `plugins/emoteplayer/emoteresourcecache.h`
- `plugins/emoteplayer/emotesession.h`
- `plugins/extrans/metaltransition.h`
- `plugins/psbfile/psbsession.h`
- `tjs2/tjsKnownByteCodeCompatibility.cpp`
- `tjs2/tjsKnownByteCodeCompatibility.h`
