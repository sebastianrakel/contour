/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2021 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <terminal/primitives.h> // ImageSize

#include <crispy/StrongHash.h>
#include <crispy/StrongLRUHashtable.h>
#include <crispy/boxed.h>

#include <variant> // monostate
#include <vector>

namespace terminal::renderer::atlas
{

using Buffer = std::vector<uint8_t>;

enum class Format
{
    Red,
    RGB,
    RGBA
};

// -----------------------------------------------------------------------
// informational data structures

struct AtlasID
{
    uint32_t value;
};

/**
 * Unique identifier of a tile in a fixed-size grid texture atlas.
 *
 * The 32-bit integer can be decomposed into two 16-bit X and Y offsets,
 * whereas Y-offset is in the most-significant 16 bits,
 * and X-offset in the least-significant 16 bits.
 *
 * With this property, the texture size of the atlas need not to be known
 * for computing the tile offset into the texture atlas.
 */
struct AtlasTileID
{
    uint32_t value;
};

/**
 * Describes the location of a tile in an atlas.
 *
 * NB: The tile-size is fixed as the atlas-grid is fixed-size.
 */
struct TileLocation
{
    // clang-format off
    struct X { uint16_t value; };
    struct Y { uint16_t value; };
    // struct RelativeX { float value; };
    // struct RelativeY { float value; };
    // struct RelativeWidth { float value; };
    // struct RelativeHeight { float value; };

    // Which atlas this tile belongs to
    AtlasID atlasID {};

    // X-offset of the tile into the texture atlas.
    X x {};

    // Y-offset of the tile into the texture atlas.
    Y y {};

    constexpr TileLocation(AtlasID aID, AtlasTileID tileID) noexcept:
        atlasID { aID },
        x { uint16_t(tileID.value & 0xFFFF) },
        y { uint16_t(tileID.value >> 16) }
    {}
    // clang-format on

    constexpr TileLocation() noexcept = default;
    constexpr TileLocation(TileLocation const&) noexcept = default;
    constexpr TileLocation(TileLocation&&) noexcept = default;
    constexpr TileLocation& operator=(TileLocation const&) noexcept = default;
    constexpr TileLocation& operator=(TileLocation&&) noexcept = default;
};

// An texture atlas is holding fixed sized tiles in a grid.
//
// The tiles are identified using a 32-bit Integer (AtlasTileID) that can
// be decomposed into X and Y coordinate pointing into the atlas texture's
// coordinate system.
struct AtlasProperties
{
    // Size in pixels of the texture atlas bitmap.
    ImageSize imageSize {};

    // Size in pixels of a tile.
    ImageSize tileSize {};

    // Human readable name for debug logging reasons.
    std::string name {};

    // Texture pixel format, such as monochrome, RGB, or RGBA.
    Format format {};

    // Number of reserved tile slots.
    //
    // This can be for example [A-Za-z0-9], characters that are most often
    // used and least likely part of a ligature.
    uint32_t reservedTileCount {};

    // Any arbitrary data to be passed to the atlas backend.
    uint32_t userdata {};
};

/// Computes the number of entries needed to store all atlas tiles.
constexpr uint32_t computeAtlasCapacity(AtlasProperties const& atlas) noexcept
{
    auto const dim = atlas.imageSize / atlas.tileSize;
    auto const total = unbox<uint32_t>(dim.width) * unbox<uint32_t>(dim.height);
    return total;
}

// -----------------------------------------------------------------------
// command data structures

// Command structure to construct a texture atlas.
struct CreateAtlas
{
    // Atlas size in pixels
    ImageSize size {};

    // internal texture format (such as GL_R8 or GL_RGBA8 when using OpenGL)
    Format format {};

    // Arbitrary user-data that CAN  be used by the AtlasBackend.
    uint32_t userdata {};
};

// Command structure to destroy a texture atlas.
struct DestroyAtlas
{
    // ID of the atlas to release the resources on the GPU for.
    AtlasID atlasID {};
};

// Command structure for uploading a tile into the texture atlas.
struct UploadTile
{
    TileLocation location;
    Buffer bitmap; // texture data to be uploaded
    ImageSize bitmapSize;
};

// Command structure for rendering a tile from a texture atlas.
struct RenderTile
{
    // clang-format off
    struct X { int value; };
    struct Y { int value; };
    // clang-format on

    X x {};                       // target X coordinate
    Y y {};                       // target Y coordinate
    TileLocation tileLocation {}; // what tile to render from which texture atlas
    std::array<float, 4> color;   // optional; a color being associated with this texture
};

// -----------------------------------------------------------------------
// interface

/// Generic listener API to events from an TextureAtlas.
/// AtlasBackend interface, performs the actual atlas operations, such as
/// texture creation, upload, render, and destruction.
///
/// @see OpenGLRenderer
class AtlasBackend
{
  public:
    virtual ~AtlasBackend() = default;

    /// Creates a new (3D) texture atlas.
    virtual AtlasID createAtlas(CreateAtlas atlas) = 0;

    /// Uploads given texture to the atlas.
    virtual void uploadTile(UploadTile tile) = 0;

    /// Renders given texture from the atlas with the given target position parameters.
    virtual void renderTile(RenderTile tile) = 0;

    /// Destroys the given (3D) texture atlas.
    virtual void destroyAtlas(AtlasID atlasID) = 0;
};

// Defines location of the tile in the atlas and its associated metadata
template <typename Metadata>
struct TileAttributes
{
    TileLocation location;
    ImageSize bitmapSize; // size of the bitmap inside the tile
    Metadata metadata;
};

/**
 * Manages the tiles of a single texture atlas.
 *
 * Atlas items are LRU-cached and the possibly passed metadata is
 * going to be destroyed at the time of cache eviction.
 *
 * The total number of of cachable tiles should be at least as large
 * as the terminal's cell count per page.
 * More tiles will most likely improve render performance.
 *
 * The metadata can be for example the render offset relative to the
 * target render base position and the actual tile size
 * (which must be smaller or equal to the tile size).
 */
template <typename Metadata = std::monostate>
class TextureAtlas
{
  public:
    TextureAtlas(AtlasBackend& backend, AtlasProperties atlasProperties);
    ~TextureAtlas();

    void reset(AtlasProperties atlasProperties);

    AtlasBackend& backend() noexcept { return _backend; }

    // Tests in LRU-cache if the tile
    constexpr bool contains(crispy::StrongHash const& _id) const noexcept;

    // Return type for in-place tile-construction callback.
    struct TileCreateData
    {
        Buffer bitmap; // RGBA bitmap data
        ImageSize bitmapSize;
        Metadata metadata;
    };

    /// Always returns either the existing item by the given key, if found,
    /// or a newly created one by invoking constructValue().
    template <typename ValueConstructFn>
    [[nodiscard]] Metadata& get_or_emplace(crispy::StrongHash const& key, ValueConstructFn constructValue);

    template <typename ValueConstructFn>
    [[nodiscard]] TileAttributes<Metadata> const* get_or_try_emplace(crispy::StrongHash const& key,
                                                                     ValueConstructFn constructValue);

    // Uploads tile data to a reserved slot in the texture atlas
    // bypassing the LRU cache.
    //
    // The tileID must be between 0 and number of reserved tiles minus 1.
    void emplace_reserved(AtlasTileID tileID, TileCreateData tileCreateData);

    // Receives a reference to the metadata of a reserved tile slot.
    //
    // The tileID must be between 0 and number of reserved tiles minus 1.
    Metadata const& get_reserved(AtlasTileID tileID) const;

  private:
    using TileCache = crispy::StrongLRUHashtable<TileAttributes<Metadata>>;
    using TileCachePtr = typename TileCache::Ptr;

    AtlasID createAtlas(AtlasProperties const& atlasProperties);

    template <typename ValueConstructFn>
    std::optional<TileAttributes<Metadata>> constructTile(ValueConstructFn fn, uint32_t entryIndex);

    AtlasProperties _atlasProperties;
    AtlasBackend& _backend;
    AtlasID _atlasID;

    // The number of entries of this cache must at most match the number
    // of tiles that can be stored into the atlas.
    TileCachePtr _tileCache;

    std::vector<TileAttributes<Metadata>> _reservedTiles;
};

// {{{ implementation

template <typename Metadata>
TextureAtlas<Metadata>::TextureAtlas(AtlasBackend& backend, AtlasProperties atlasProperties):
    _atlasProperties { std::move(atlasProperties) },
    _backend { backend },
    _atlasID { createAtlas(atlasProperties) },
    _tileCache { TileCache::create(crispy::StrongHashtableSize { 2 * computeAtlasCapacity(_atlasProperties) },
                                   crispy::LRUCapacity { computeAtlasCapacity(_atlasProperties) }) }
{
    _reservedTiles.resize(_atlasProperties.reservedTileCount);
}

template <typename Metadata>
TextureAtlas<Metadata>::~TextureAtlas()
{
    _backend.destroyAtlas(_atlasID);
}

template <typename Metadata>
constexpr bool TextureAtlas<Metadata>::contains(crispy::StrongHash const& _id) const noexcept
{
    return _tileCache->contains(_id);
}

template <typename Metadata>
template <typename ValueConstructFn>
auto TextureAtlas<Metadata>::constructTile(ValueConstructFn creator, uint32_t entryIndex)
    -> std::optional<TileAttributes<Metadata>>
{
    // The StrongLRUHashtable's passed entryIndex can be used
    // to construct the texture atlas' tile coordinates.

    auto const tileLocation =
        TileLocation(_atlasID, AtlasTileID { entryIndex + _atlasProperties.reservedTileCount });

    std::optional<TileCreateData> tileCreateDataOpt = creator(tileLocation);
    if (!tileCreateDataOpt)
        return std::nullopt;

    TileCreateData& tileCreateData = *tileCreateDataOpt;

    UploadTile tileUpload {};
    tileUpload.location = tileLocation;
    tileUpload.bitmapSize = tileCreateData.bitmapSize;
    tileUpload.bitmap = std::move(tileCreateData.bitmap);
    _backend.uploadTile(std::move(tileUpload));

    TileAttributes<Metadata> instance {};
    instance.location = tileLocation;
    instance.bitmapSize = tileCreateData.bitmapSize;
    instance.metadata = std::move(tileCreateData.metadata);
    return instance;
}

template <typename Metadata>
template <typename ValueConstructFn>
[[nodiscard]] Metadata& TextureAtlas<Metadata>::get_or_emplace(crispy::StrongHash const& key,
                                                               ValueConstructFn createTileData)
{
    auto create = [&](uint32_t entryIndex) -> std::optional<TileAttributes<Metadata>> {
        return constructTile(std::move(createTileData), entryIndex);
    };

    return _tileCache->get_or_emplace(key, std::move(create)).metadata;
}

template <typename Metadata>
template <typename ValueConstructFn>
[[nodiscard]] TileAttributes<Metadata> const* TextureAtlas<Metadata>::get_or_try_emplace(
    crispy::StrongHash const& key, ValueConstructFn createTileData)
{
    auto create = [&](uint32_t entryIndex) -> std::optional<TileAttributes<Metadata>> {
        return constructTile(std::move(createTileData), entryIndex);
    };

    return _tileCache->get_or_try_emplace(key, std::move(create));
}

template <typename Metadata>
void TextureAtlas<Metadata>::reset(AtlasProperties atlasProperties)
{
    bool const needsRecreate = _atlasProperties.imageSize != atlasProperties.imageSize;

    if (needsRecreate)
        _backend.destroyAtlas(_atlasID);

    _atlasProperties = std::move(atlasProperties);
    _tileCache->clear();

    if (needsRecreate)
        _atlasID = createAtlas(_atlasProperties);
}

template <typename Metadata>
AtlasID TextureAtlas<Metadata>::createAtlas(AtlasProperties const& atlasProperties)
{
    auto atlasTextureProperties = CreateAtlas {};
    atlasTextureProperties.size = _atlasProperties.imageSize;
    atlasTextureProperties.format = _atlasProperties.format;
    atlasTextureProperties.userdata = _atlasProperties.userdata;
    return _backend.createAtlas(atlasTextureProperties);
}

template <typename Metadata>
Metadata const& TextureAtlas<Metadata>::get_reserved(AtlasTileID tileID) const
{
    Require(tileID.value < _reservedTiles.size());
    return _reservedTiles[tileID.value];
}

template <typename Metadata>
void TextureAtlas<Metadata>::emplace_reserved(AtlasTileID tileID, TileCreateData tileCreateData)
{
    Require(tileID.value < _reservedTiles.size());

    auto const tileLocation = TileLocation(_atlasID, tileID);

    // clang-format off
    _reservedTiles[tileID.value] = TileAttributes<Metadata>{
        tileLocation,
        std::move(tileCreateData.metadata)
    };

    _backend.uploadTile(UploadTile{
        tileLocation,
        std::move(tileCreateData.bitmap),
        ImageSize{} // TODO(pr) bitmap size
    });
    // clang-format on
}
// }}}

// }}}
} // namespace terminal::renderer::atlas
