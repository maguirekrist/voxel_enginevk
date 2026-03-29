#include <gtest/gtest.h>

#include <filesystem>
#include <ranges>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "ui/ui_font_backend_freetype.h"
#include "ui/ui_font_backend_msdf.h"
#include "ui/ui_runtime.h"

namespace
{
    class FakeFontBackend final : public ui::FontBackend
    {
    public:
        explicit FakeFontBackend(const ui::FontSourceFormat supportedFormat, const ui::FontRasterizationMode supportedMode) :
            _supportedFormat(supportedFormat),
            _supportedMode(supportedMode)
        {
        }

        [[nodiscard]] std::string_view backend_name() const override
        {
            return "fake";
        }

        [[nodiscard]] bool supports_source_format(const ui::FontSourceFormat format) const override
        {
            return format == _supportedFormat;
        }

        [[nodiscard]] bool supports_rasterization(const ui::FontRasterizationMode mode) const override
        {
            return mode == _supportedMode;
        }

        [[nodiscard]] bool can_load_face(const ui::FontFaceDescriptor& face) const override
        {
            return std::ranges::any_of(face.sources, [&](const ui::FontAssetSource& source)
            {
                return source.format == _supportedFormat && face.rasterizationMode == _supportedMode;
            });
        }

        [[nodiscard]] bool supports_atlas_type(const ui::FontAtlasType) const override
        {
            return false;
        }

        [[nodiscard]] ui::TextMetrics measure_text(const ui::TextRun& run, const ui::FontFaceDescriptor&) const override
        {
            return ui::TextMetrics{
                .size = glm::vec2(static_cast<float>(run.text.size()) * run.pixelHeight * 0.5f, run.pixelHeight),
                .ascent = run.pixelHeight * 0.8f,
                .descent = run.pixelHeight * 0.2f,
                .lineGap = run.pixelHeight * 0.1f
            };
        }

        [[nodiscard]] std::optional<ui::ShapedText> shape_text(const ui::TextRun& run, const ui::FontFaceDescriptor& face) const override
        {
            ui::ShapedText shaped{
                .faceId = face.id,
                .metrics = measure_text(run, face)
            };

            for (size_t i = 0; i < run.text.size(); ++i)
            {
                shaped.glyphs.push_back(ui::ShapedGlyph{
                    .glyphIndex = static_cast<uint32_t>(run.text[i]),
                    .cluster = static_cast<uint32_t>(i),
                    .advance = glm::vec2(run.pixelHeight * 0.5f, 0.0f)
                });
            }

            return shaped;
        }

        [[nodiscard]] std::optional<ui::GeneratedFontAtlas> generate_atlas(const ui::FontFaceDescriptor&, const ui::FontAtlasRequest&) const override
        {
            return std::nullopt;
        }

    private:
        ui::FontSourceFormat _supportedFormat;
        ui::FontRasterizationMode _supportedMode;
    };
}

TEST(UiStableIdTest, HashesAreStableForRepeatedInputs)
{
    const ui::ElementId first = ui::make_element_id("player.health");
    const ui::ElementId second = ui::make_element_id("player.health");
    const ui::ElementId third = ui::make_element_id("player.mana");

    EXPECT_EQ(first, second);
    EXPECT_NE(first, third);
    EXPECT_TRUE(first.valid());
}

TEST(UiLayoutTest, ResolveRectHonorsAnchorAndOffset)
{
    const ui::Rect rect = ui::resolve_rect(
        ui::LayoutBox{
            .size = glm::vec2(100.0f, 20.0f),
            .anchor = ui::Anchor{
                .horizontal = ui::HorizontalAnchor::Right,
                .vertical = ui::VerticalAnchor::Bottom,
                .offset = glm::vec2(-10.0f, -5.0f)
            }
        },
        ui::Extent2D{ .width = 800, .height = 600 });

    EXPECT_FLOAT_EQ(rect.min.x, 690.0f);
    EXPECT_FLOAT_EQ(rect.min.y, 575.0f);
    EXPECT_FLOAT_EQ(rect.max.x, 790.0f);
    EXPECT_FLOAT_EQ(rect.max.y, 595.0f);
}

TEST(UiRuntimeTest, RetainsFocusButClearsHoverAtFrameStart)
{
    ui::Runtime runtime;
    const ui::ElementId focused = ui::make_element_id("focused");
    const ui::ElementId hovered = ui::make_element_id("hovered");

    runtime.set_focused_element(focused);
    runtime.set_hovered_element(hovered);
    runtime.begin_frame(ui::FrameDescriptor{
        .viewport = ui::Extent2D{ .width = 1280, .height = 720 },
        .frameIndex = 7
    });

    EXPECT_EQ(runtime.interaction_state().focusedElement, focused);
    EXPECT_FALSE(runtime.interaction_state().hoveredElement.has_value());
    EXPECT_TRUE(runtime.is_frame_active());
    EXPECT_EQ(runtime.current_frame().descriptor.frameIndex, 7u);
}

TEST(UiRuntimeTest, SignalsSubmittedBeforeAndDuringFrameReachTheSnapshot)
{
    ui::Runtime runtime;
    runtime.submit_signal(ui::Signal{
        .id = ui::make_signal_id("pre-frame"),
        .kind = ui::SignalKind::Toast,
        .channel = "toast"
    });

    runtime.begin_frame(ui::FrameDescriptor{
        .viewport = ui::Extent2D{ .width = 640, .height = 360 },
        .frameIndex = 3
    });
    runtime.submit_signal(ui::Signal{
        .id = ui::make_signal_id("in-frame"),
        .kind = ui::SignalKind::Pulse,
        .channel = "pulse"
    });

    ASSERT_EQ(runtime.current_frame().signals.size(), 2u);
    EXPECT_EQ(runtime.current_frame().signals[0].id, ui::make_signal_id("pre-frame"));
    EXPECT_EQ(runtime.current_frame().signals[1].id, ui::make_signal_id("in-frame"));
}

TEST(UiRuntimeTest, BuilderAcceptsPullModelsAndWorldLabelsInSameFrame)
{
    ui::Runtime runtime;
    runtime.begin_frame(ui::FrameDescriptor{
        .viewport = ui::Extent2D{ .width = 1024, .height = 768 },
        .frameIndex = 99
    });

    ui::FrameBuilder builder = runtime.frame_builder();
    builder.declare_screen(ui::make_screen_id("hud"), "HUD");
    builder.declare_layer(ui::make_layer_id("hud.base"), "HUD Base", ui::LayerInputPolicy::Passthrough, 10);
    builder.submit_model(ui::make_model_id("hud.health"), ui::make_element_id("health"), "Health Model", 75);
    builder.submit_draw_command(ui::DrawCommand{
        .payload = ui::QuadCommand{
            .rect = ui::Rect{ .min = glm::vec2(12.0f, 12.0f), .max = glm::vec2(120.0f, 36.0f) },
            .layerId = ui::make_layer_id("hud.base")
        }
    });

    ui::WorldLabelCollector collector = runtime.world_label_collector();
    collector.add_label(ui::WorldLabel{
        .id = ui::make_element_id("npc.greeting"),
        .screenId = ui::make_screen_id("hud"),
        .mode = ui::WorldLabelMode::ProjectedOverlay,
        .text = ui::TextRun{
            .text = "Hello",
            .familyId = ui::make_font_family_id("default")
        }
    });

    const ui::FrameSnapshot& frame = runtime.current_frame();
    ASSERT_EQ(frame.screens.size(), 1u);
    ASSERT_EQ(frame.layers.size(), 1u);
    ASSERT_EQ(frame.models.size(), 1u);
    ASSERT_EQ(frame.drawCommands.size(), 1u);
    ASSERT_EQ(frame.worldLabels.size(), 1u);
    EXPECT_EQ(std::any_cast<int>(frame.models.front().payload), 75);
}

TEST(UiTextSystemTest, PreparesTextCommandsIntoGlyphQuadsFromMsdfAtlases)
{
    ui::TextSystem textSystem;
    textSystem.register_backend(std::make_shared<ui::MsdfAtlasFontBackend>());

    const std::filesystem::path fontPath = std::filesystem::absolute("third_party/imgui/misc/fonts/ProggyClean.ttf");
    const ui::FontFaceId faceId = ui::make_font_face_id("runtime.proggy");
    const ui::FontFamilyId familyId = ui::make_font_family_id("runtime.default");

    textSystem.register_face(ui::FontFaceDescriptor{
        .id = faceId,
        .debugName = "Runtime Proggy",
        .rasterizationMode = ui::FontRasterizationMode::Vector,
        .nominalPixelHeight = 18.0f,
        .sources = {
            ui::FontAssetSource{
                .debugName = "Runtime Proggy TTF",
                .path = fontPath,
                .format = ui::FontSourceFormat::TrueType
            }
        }
    });
    textSystem.register_family(ui::FontFamily{
        .id = familyId,
        .debugName = "Runtime Default",
        .preferredFaces = { faceId }
    });

    const std::optional<ui::PreparedTextCommand> prepared = textSystem.prepare_text_command(ui::TextCommand{
        .rect = ui::Rect{
            .min = glm::vec2(0.0f, 0.0f),
            .max = glm::vec2(240.0f, 48.0f)
        },
        .text = ui::TextRun{
            .text = "Hello runtime UI",
            .familyId = familyId,
            .pixelHeight = 18.0f,
            .languageTag = "en"
        }
    });

    ASSERT_TRUE(prepared.has_value());
    EXPECT_EQ(prepared->faceId, faceId);
    EXPECT_EQ(prepared->atlasType, ui::FontAtlasType::MultiChannelSignedDistanceField);
    EXPECT_GT(prepared->metrics.size.x, 0.0f);
    EXPECT_FALSE(prepared->glyphQuads.empty());
    for (const ui::PreparedGlyphQuad& glyph : prepared->glyphQuads)
    {
        EXPECT_LT(glyph.rect.min.y, glyph.rect.max.y);
        EXPECT_LT(glyph.uvBounds.y, glyph.uvBounds.w);
    }

    const ui::GeneratedFontAtlas* atlas = textSystem.find_generated_atlas(faceId, ui::FontAtlasType::MultiChannelSignedDistanceField);
    ASSERT_NE(atlas, nullptr);
    EXPECT_GT(atlas->bitmap.width, 0);
    EXPECT_GT(atlas->bitmap.height, 0);
    EXPECT_EQ(atlas->bitmap.channels, 3);
    EXPECT_FALSE(atlas->glyphs.empty());
    const auto firstVisibleGlyph = std::ranges::find_if(atlas->glyphs, [](const ui::FontAtlasGlyph& glyph)
    {
        return glyph.atlasBounds.z > glyph.atlasBounds.x && glyph.atlasBounds.w > glyph.atlasBounds.y;
    });
    ASSERT_NE(firstVisibleGlyph, atlas->glyphs.end());
    EXPECT_LT(firstVisibleGlyph->planeBounds.y, firstVisibleGlyph->planeBounds.w);
    EXPECT_LT(firstVisibleGlyph->atlasBounds.y, firstVisibleGlyph->atlasBounds.w);
}

TEST(UiRuntimeTest, FinalizeFrameBuildsPreparedTextCommandsWhenFontsAreRegistered)
{
    ui::Runtime runtime;
    runtime.text_system().register_backend(std::make_shared<ui::MsdfAtlasFontBackend>());

    const std::filesystem::path fontPath = std::filesystem::absolute("third_party/imgui/misc/fonts/ProggyClean.ttf");
    const ui::FontFaceId faceId = ui::make_font_face_id("runtime.frame.proggy");
    const ui::FontFamilyId familyId = ui::make_font_family_id("runtime.frame.default");

    runtime.text_system().register_face(ui::FontFaceDescriptor{
        .id = faceId,
        .debugName = "Runtime Frame Proggy",
        .rasterizationMode = ui::FontRasterizationMode::Vector,
        .nominalPixelHeight = 16.0f,
        .sources = {
            ui::FontAssetSource{
                .debugName = "Runtime Frame Proggy TTF",
                .path = fontPath,
                .format = ui::FontSourceFormat::TrueType
            }
        }
    });
    runtime.text_system().register_family(ui::FontFamily{
        .id = familyId,
        .debugName = "Runtime Frame Default",
        .preferredFaces = { faceId }
    });

    runtime.begin_frame(ui::FrameDescriptor{
        .viewport = ui::Extent2D{ .width = 800, .height = 600 },
        .frameIndex = 12
    });
    runtime.frame_builder().submit_draw_command(ui::DrawCommand{
        .payload = ui::TextCommand{
            .rect = ui::Rect{
                .min = glm::vec2(20.0f, 20.0f),
                .max = glm::vec2(220.0f, 52.0f)
            },
            .text = ui::TextRun{
                .text = "Runtime text",
                .familyId = familyId,
                .pixelHeight = 16.0f
            }
        }
    });

    runtime.finalize_frame();

    ASSERT_EQ(runtime.current_frame().preparedTextCommands.size(), 1u);
    EXPECT_FALSE(runtime.current_frame().preparedTextCommands.front().glyphQuads.empty());
}

TEST(UiFontCatalogTest, ResolvesBackendCompatibleFacesAcrossPreferredAndFallbackChains)
{
    ui::FontCatalog catalog;
    catalog.register_face(ui::FontFaceDescriptor{
        .id = ui::make_font_face_id("pixel-primary"),
        .debugName = "Pixel Primary",
        .rasterizationMode = ui::FontRasterizationMode::Bitmap,
        .sources = {
            ui::FontAssetSource{
                .debugName = "pixel-atlas",
                .path = "fonts/pixel.png",
                .format = ui::FontSourceFormat::BitmapAtlas
            }
        }
    });
    catalog.register_face(ui::FontFaceDescriptor{
        .id = ui::make_font_face_id("unicode-fallback"),
        .debugName = "Unicode Fallback",
        .rasterizationMode = ui::FontRasterizationMode::Vector,
        .sources = {
            ui::FontAssetSource{
                .debugName = "unicode-ttf",
                .path = "fonts/unicode.ttf",
                .format = ui::FontSourceFormat::TrueType
            }
        }
    });
    catalog.register_family(ui::FontFamily{
        .id = ui::make_font_family_id("dialog"),
        .debugName = "Dialog",
        .preferredFaces = { ui::make_font_face_id("pixel-primary") },
        .fallbackFaces = { ui::make_font_face_id("unicode-fallback") }
    });

    const FakeFontBackend bitmapBackend(ui::FontSourceFormat::BitmapAtlas, ui::FontRasterizationMode::Bitmap);
    const FakeFontBackend vectorBackend(ui::FontSourceFormat::TrueType, ui::FontRasterizationMode::Vector);

    EXPECT_EQ(catalog.resolve_face_for_backend(ui::make_font_family_id("dialog"), bitmapBackend), ui::make_font_face_id("pixel-primary"));
    EXPECT_EQ(catalog.resolve_face_for_backend(ui::make_font_family_id("dialog"), vectorBackend), ui::make_font_face_id("unicode-fallback"));
}

TEST(UiFontBackendRegistryTest, FindsRegisteredBackendsByName)
{
    ui::FontBackendRegistry registry;
    registry.register_backend(std::make_shared<FakeFontBackend>(ui::FontSourceFormat::BitmapAtlas, ui::FontRasterizationMode::Bitmap));

    ASSERT_NE(registry.find_backend("fake"), nullptr);
    EXPECT_EQ(registry.find_backend("missing"), nullptr);
}

TEST(FreeTypeHarfBuzzFontBackendTest, LoadsTrueTypeFontsAndProducesMeasurementsAndGlyphs)
{
    ui::FreeTypeHarfBuzzFontBackend backend;
    const std::filesystem::path fontPath = std::filesystem::absolute("third_party/imgui/misc/fonts/ProggyClean.ttf");

    ui::FontFaceDescriptor face{
        .id = ui::make_font_face_id("proggy"),
        .debugName = "Proggy Clean",
        .rasterizationMode = ui::FontRasterizationMode::Vector,
        .sources = {
            ui::FontAssetSource{
                .debugName = "Proggy Clean TTF",
                .path = fontPath,
                .format = ui::FontSourceFormat::TrueType
            }
        }
    };

    const ui::TextRun run{
        .text = "Hello runtime UI",
        .familyId = ui::make_font_family_id("test"),
        .pixelHeight = 18.0f,
        .languageTag = "en"
    };

    ASSERT_TRUE(backend.can_load_face(face));
    const ui::TextMetrics metrics = backend.measure_text(run, face);
    const std::optional<ui::ShapedText> shaped = backend.shape_text(run, face);

    EXPECT_GT(metrics.size.x, 0.0f);
    EXPECT_GT(metrics.size.y, 0.0f);
    ASSERT_TRUE(shaped.has_value());
    EXPECT_EQ(shaped->faceId, face.id);
    EXPECT_FALSE(shaped->glyphs.empty());
    EXPECT_GT(shaped->metrics.ascent, 0.0f);
}

TEST(MsdfAtlasFontBackendTest, GeneratesMsdfAtlasesFromTrueTypeFonts)
{
    ui::MsdfAtlasFontBackend backend;
    const std::filesystem::path fontPath = std::filesystem::absolute("third_party/imgui/misc/fonts/ProggyClean.ttf");

    ui::FontFaceDescriptor face{
        .id = ui::make_font_face_id("proggy-msdf"),
        .debugName = "Proggy Clean",
        .rasterizationMode = ui::FontRasterizationMode::Vector,
        .sources = {
            ui::FontAssetSource{
                .debugName = "Proggy Clean TTF",
                .path = fontPath,
                .format = ui::FontSourceFormat::TrueType
            }
        }
    };

    const ui::FontAtlasRequest request{
        .codepoints = { 'H', 'e', 'l', 'o', ' ', 'U', 'I' },
        .atlasType = ui::FontAtlasType::MultiChannelSignedDistanceField,
        .minimumScale = 24.0f,
        .pixelRange = 2.0f,
        .threadCount = 1
    };

    ASSERT_TRUE(backend.can_load_face(face));
    ASSERT_TRUE(backend.supports_atlas_type(request.atlasType));

    const std::optional<ui::GeneratedFontAtlas> atlas = backend.generate_atlas(face, request);
    ASSERT_TRUE(atlas.has_value());
    EXPECT_EQ(atlas->faceId, face.id);
    EXPECT_EQ(atlas->atlasType, request.atlasType);
    EXPECT_GT(atlas->bitmap.width, 0);
    EXPECT_GT(atlas->bitmap.height, 0);
    EXPECT_EQ(atlas->bitmap.channels, 3);
    EXPECT_FALSE(atlas->bitmap.pixels.empty());
    EXPECT_FALSE(atlas->glyphs.empty());

    const auto glyphForH = std::ranges::find_if(atlas->glyphs, [](const ui::FontAtlasGlyph& glyph)
    {
        return glyph.codePoint == static_cast<uint32_t>('H');
    });
    ASSERT_NE(glyphForH, atlas->glyphs.end());
    EXPECT_GT(glyphForH->advance, 0.0f);
    EXPECT_LT(glyphForH->planeBounds.y, glyphForH->planeBounds.w);
    EXPECT_LT(glyphForH->atlasBounds.y, glyphForH->atlasBounds.w);
}

TEST(UiWorldProjectionTest, ProjectsVisibleAnchorsAndRejectsBehindCameraAnchors)
{
    const glm::mat4 projection = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    const ui::ProjectionParameters params{
        .viewProjection = projection * view,
        .viewport = ui::Extent2D{ .width = 1920, .height = 1080 },
        .cameraPosition = glm::vec3(0.0f)
    };

    const std::optional<glm::vec2> visible = ui::project_world_anchor(
        ui::WorldAnchor{
            .worldPosition = glm::vec3(0.0f, 0.0f, -5.0f)
        },
        params);
    ASSERT_TRUE(visible.has_value());
    EXPECT_NEAR(visible->x, 960.0f, 1.5f);
    EXPECT_NEAR(visible->y, 540.0f, 1.5f);

    const std::optional<glm::vec2> hidden = ui::project_world_anchor(
        ui::WorldAnchor{
            .worldPosition = glm::vec3(0.0f, 0.0f, 5.0f)
        },
        params);
    EXPECT_FALSE(hidden.has_value());
}
