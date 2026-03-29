#include "ui_text.h"

#include <algorithm>
#include <ranges>

namespace ui
{
    void FontCatalog::register_face(FontFaceDescriptor face)
    {
        _faces[face.id] = std::move(face);
    }

    void FontCatalog::register_family(FontFamily family)
    {
        _families[family.id] = std::move(family);
    }

    const FontFaceDescriptor* FontCatalog::find_face(const FontFaceId id) const
    {
        const auto it = _faces.find(id);
        if (it == _faces.end())
        {
            return nullptr;
        }

        return &it->second;
    }

    const FontFamily* FontCatalog::find_family(const FontFamilyId id) const
    {
        const auto it = _families.find(id);
        if (it == _families.end())
        {
            return nullptr;
        }

        return &it->second;
    }

    std::vector<const FontFaceDescriptor*> FontCatalog::resolve_face_chain(const FontFamilyId familyId) const
    {
        std::vector<const FontFaceDescriptor*> chain{};

        const FontFamily* family = find_family(familyId);
        if (family == nullptr)
        {
            return chain;
        }

        auto append_face = [&](const FontFaceId faceId)
        {
            const FontFaceDescriptor* face = find_face(faceId);
            if (face == nullptr)
            {
                return;
            }

            const auto duplicate = std::ranges::find(chain, face);
            if (duplicate == chain.end())
            {
                chain.push_back(face);
            }
        };

        for (const FontFaceId faceId : family->preferredFaces)
        {
            append_face(faceId);
        }

        for (const FontFaceId faceId : family->fallbackFaces)
        {
            append_face(faceId);
        }

        return chain;
    }

    std::optional<FontFaceId> FontCatalog::resolve_face_for_backend(const FontFamilyId familyId, const FontBackend& backend) const
    {
        for (const FontFaceDescriptor* face : resolve_face_chain(familyId))
        {
            if (!backend.supports_rasterization(face->rasterizationMode))
            {
                continue;
            }

            const bool hasCompatibleSource = std::ranges::any_of(face->sources, [&](const FontAssetSource& source)
            {
                return backend.supports_source_format(source.format);
            });

            if (hasCompatibleSource)
            {
                return face->id;
            }
        }

        return std::nullopt;
    }

    void FontBackendRegistry::register_backend(std::shared_ptr<FontBackend> backend)
    {
        if (backend == nullptr)
        {
            return;
        }

        const auto duplicate = std::ranges::find_if(_backends, [&](const std::shared_ptr<FontBackend>& existing)
        {
            return existing != nullptr && existing->backend_name() == backend->backend_name();
        });

        if (duplicate != _backends.end())
        {
            *duplicate = std::move(backend);
            return;
        }

        _backends.push_back(std::move(backend));
    }

    std::shared_ptr<FontBackend> FontBackendRegistry::find_backend(const std::string_view backendName) const
    {
        const auto it = std::ranges::find_if(_backends, [&](const std::shared_ptr<FontBackend>& backend)
        {
            return backend != nullptr && backend->backend_name() == backendName;
        });

        if (it == _backends.end())
        {
            return nullptr;
        }

        return *it;
    }

    const std::vector<std::shared_ptr<FontBackend>>& FontBackendRegistry::backends() const noexcept
    {
        return _backends;
    }
}
