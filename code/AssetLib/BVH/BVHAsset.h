/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2022, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

/** @file BVHAsset.h
 * Declares a BVH class to handle bvh files
 *
 */
#ifndef BVHASSET_H_INC
#define BVHASSET_H_INC

#if !defined(ASSIMP_BUILD_NO_BVH_IMPORTER)

#include "BVHCommon.h"
#include <assimp/Exceptional.h>
#include <algorithm>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// clang-format off

#if (__GNUC__ == 8 && __GNUC_MINOR__ >= 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

#if (__GNUC__ == 8 && __GNUC_MINOR__ >= 0)
#pragma GCC diagnostic pop
#endif

#ifdef ASSIMP_API
#   include <memory>
#   include <assimp/DefaultIOSystem.h>
#   include <assimp/ByteSwapper.h>
#else
#   include <memory>
#   define AI_SWAP4(p)
#   define ai_assert
#endif


#if _MSC_VER > 1500 || (defined __GNUC___)
#       define ASSIMP_BVH_USE_UNORDERED_MULTIMAP
#   else
#       define bvh_unordered_map map
#endif

#ifdef ASSIMP_BVH_USE_UNORDERED_MULTIMAP
#   include <unordered_map>
#   if defined(_MSC_VER) && _MSC_VER <= 1600
#       define bvh_unordered_map tr1::unordered_map
#   else
#       define bvh_unordered_map unordered_map
#   endif
#endif

// clang-format on

#include "AssetLib/BVH/BVHCommon.h"

namespace BVH {

using BVHCommon::IOStream;
using BVHCommon::IOSystem;
using BVHCommon::Nullable;
using BVHCommon::Ref;
using BVHCommon::shared_ptr;

class Asset;
class AssetWriter;

struct BufferView; // here due to cross-reference
struct Texture;
struct Light;
struct Skin;

using BVHCommon::mat4;
using BVHCommon::vec3;
using BVHCommon::vec4;

//! Magic number for GLB files
#define AI_GLB_MAGIC_NUMBER "glTF"

// clang-format off
#ifdef ASSIMP_API
#   include <assimp/Compiler/pushpack1.h>
#endif
// clang-format on

// clang-format off
#ifdef ASSIMP_API
#   include <assimp/Compiler/poppack1.h>
#endif
// clang-format on

//
// Classes for each BVH top-level object type
//

//! A typed view into a BufferView. A BufferView contains raw binary data.
//! An accessor provides a typed view into a BufferView or a subset of a BufferView
//! similar to how WebGL's vertexAttribPointer() defines an attribute in a buffer.
struct Accessor : public Object {
    Ref<BufferView> bufferView; //!< The ID of the bufferView. (required)
    unsigned int byteOffset; //!< The offset relative to the start of the bufferView in bytes. (required)
    unsigned int byteStride; //!< The stride, in bytes, between attributes referenced by this accessor. (default: 0)
    ComponentType componentType; //!< The datatype of components in the attribute. (required)
    unsigned int count; //!< The number of attributes referenced by this accessor. (required)
    AttribType::Value type; //!< Specifies if the attribute is a scalar, vector, or matrix. (required)
    std::vector<double> max; //!< Maximum value of each component in this attribute.
    std::vector<double> min; //!< Minimum value of each component in this attribute.

    unsigned int GetNumComponents();
    unsigned int GetBytesPerComponent();
    unsigned int GetElementSize();

    inline uint8_t *GetPointer();

    template <class T>
    bool ExtractData(T *&outData);

    void WriteData(size_t count, const void *src_buffer, size_t src_stride);

    //! Helper class to iterate the data
    class Indexer {
        friend struct Accessor;

        // This field is reported as not used, making it protectd is the easiest way to work around it without going to the bottom of what the problem is:
        // ../code/glTF2/glTF2Asset.h:392:19: error: private field 'accessor' is not used [-Werror,-Wunused-private-field]
    protected:
        Accessor &accessor;

    private:
        uint8_t *data;
        size_t elemSize, stride;

        Indexer(Accessor &acc);

    public:
        //! Accesses the i-th value as defined by the accessor
        template <class T>
        T GetValue(int i);

        //! Accesses the i-th value as defined by the accessor
        inline unsigned int GetUInt(int i) {
            return GetValue<unsigned int>(i);
        }

        inline bool IsValid() const {
            return data != 0;
        }
    };

    inline Indexer GetIndexer() {
        return Indexer(*this);
    }

    Accessor() = default;
    void Read(Value &obj, Asset &r);
};

//! A buffer points to binary geometry, animation, or skins.
struct Buffer : public Object {
    /********************* Types *********************/
    enum Type {
        Type_arraybuffer,
        Type_text
    };

    /// @brief  Descriptor of encoded region in "bufferView".
    struct SEncodedRegion {
        const size_t Offset; ///< Offset from begin of "bufferView" to encoded region, in bytes.
        const size_t EncodedData_Length; ///< Size of encoded region, in bytes.
        uint8_t *const DecodedData; ///< Cached encoded data.
        const size_t DecodedData_Length; ///< Size of decoded region, in bytes.
        const std::string ID; ///< ID of the region.

        /// @brief Constructor.
        /// \param [in] pOffset - offset from begin of "bufferView" to encoded region, in bytes.
        /// \param [in] pEncodedData_Length - size of encoded region, in bytes.
        /// \param [in] pDecodedData - pointer to decoded data array.
        /// \param [in] pDecodedData_Length - size of encoded region, in bytes.
        /// \param [in] pID - ID of the region.
        SEncodedRegion(const size_t pOffset, const size_t pEncodedData_Length, uint8_t *pDecodedData, const size_t pDecodedData_Length, const std::string &pID) :
                Offset(pOffset), EncodedData_Length(pEncodedData_Length), DecodedData(pDecodedData), DecodedData_Length(pDecodedData_Length), ID(pID) {}

        /// Destructor.
        ~SEncodedRegion() { delete[] DecodedData; }
    };

    /******************* Variables *******************/

    size_t byteLength; //!< The length of the buffer in bytes. (default: 0)

    Type type;

    /// \var EncodedRegion_Current
    /// Pointer to currently active encoded region.
    /// Why not decoding all regions at once and not to set one buffer with decoded data?
    /// Yes, why not? Even "accessor" point to decoded data. I mean that fields "byteOffset", "byteStride" and "count" has values which describes decoded
    /// data array. But only in range of mesh while is active parameters from "compressedData". For another mesh accessors point to decoded data too. But
    /// offset is counted for another regions is encoded.
    /// Example. You have two meshes. For every of it you have 4 bytes of data. That data compressed to 2 bytes. So, you have buffer with encoded data:
    /// M1_E0, M1_E1, M2_E0, M2_E1.
    /// After decoding you'll get:
    /// M1_D0, M1_D1, M1_D2, M1_D3, M2_D0, M2_D1, M2_D2, M2_D3.
    /// "accessors" must to use values that point to decoded data - obviously. So, you'll expect "accessors" like
    /// "accessor_0" : { byteOffset: 0, byteLength: 4}, "accessor_1" : { byteOffset: 4, byteLength: 4}
    /// but in real life you'll get:
    /// "accessor_0" : { byteOffset: 0, byteLength: 4}, "accessor_1" : { byteOffset: 2, byteLength: 4}
    /// Yes, accessor of next mesh has offset and length which mean: current mesh data is decoded, all other data is encoded.
    /// And when before you start to read data of current mesh (with encoded data ofcourse) you must decode region of "bufferView", after read finished
    /// delete encoding mark. And after that you can repeat process: decode data of mesh, read, delete decoded data.
    ///
    /// Remark. Encoding all data at once is good in world with computers which do not has RAM limitation. So, you must use step by step encoding in
    /// exporter and importer. And, thanks to such way, there is no need to load whole file into memory.
    SEncodedRegion *EncodedRegion_Current;

private:
    shared_ptr<uint8_t> mData; //!< Pointer to the data
    bool mIsSpecial; //!< Set to true for special cases (e.g. the body buffer)
    size_t capacity = 0; //!< The capacity of the buffer in bytes. (default: 0)
    /// \var EncodedRegion_List
    /// List of encoded regions.
    std::list<SEncodedRegion *> EncodedRegion_List;

    /******************* Functions *******************/

public:
    Buffer();
    ~Buffer();

    void Read(Value &obj, Asset &r);

    bool LoadFromStream(IOStream &stream, size_t length = 0, size_t baseOffset = 0);

    /// Mark region of "bufferView" as encoded. When data is request from such region then "bufferView" use decoded data.
    /// \param [in] pOffset - offset from begin of "bufferView" to encoded region, in bytes.
    /// \param [in] pEncodedData_Length - size of encoded region, in bytes.
    /// \param [in] pDecodedData - pointer to decoded data array.
    /// \param [in] pDecodedData_Length - size of encoded region, in bytes.
    /// \param [in] pID - ID of the region.
    void EncodedRegion_Mark(const size_t pOffset, const size_t pEncodedData_Length, uint8_t *pDecodedData, const size_t pDecodedData_Length, const std::string &pID);

    /// Select current encoded region by ID. \sa EncodedRegion_Current.
    /// \param [in] pID - ID of the region.
    void EncodedRegion_SetCurrent(const std::string &pID);

    /// Replace part of buffer data. Pay attention that function work with original array of data (\ref mData) not with encoded regions.
    /// \param [in] pBufferData_Offset - index of first element in buffer from which new data will be placed.
    /// \param [in] pBufferData_Count - count of bytes in buffer which will be replaced.
    /// \param [in] pReplace_Data - pointer to array with new data for buffer.
    /// \param [in] pReplace_Count - count of bytes in new data.
    /// \return true - if successfully replaced, false if input arguments is out of range.
    bool ReplaceData(const size_t pBufferData_Offset, const size_t pBufferData_Count, const uint8_t *pReplace_Data, const size_t pReplace_Count);

    size_t AppendData(uint8_t *data, size_t length);
    void Grow(size_t amount);

    uint8_t *GetPointer() { return mData.get(); }

    void MarkAsSpecial() { mIsSpecial = true; }

    bool IsSpecial() const { return mIsSpecial; }

    std::string GetURI() { return std::string(this->id) + ".bin"; }

    static const char *TranslateId(Asset &r, const char *id);
};

//! A view into a buffer generally representing a subset of the buffer.
struct BufferView : public Object {
    Ref<Buffer> buffer; //! The ID of the buffer. (required)
    size_t byteOffset; //! The offset into the buffer in bytes. (required)
    size_t byteLength; //! The length of the bufferView in bytes. (default: 0)

    BufferViewTarget target; //! The target that the WebGL buffer should be bound to.

    void Read(Value &obj, Asset &r);
};

//! A set of primitives to be rendered. A node can contain one or more meshes. A node's transform places the mesh in the scene.
struct Mesh : public Object {
    typedef std::vector<Ref<Accessor>> AccessorList;

    struct Primitive {
        PrimitiveMode mode;

        struct Attributes {
            AccessorList position, normal, texcoord, color, joint, jointmatrix, weight;
        } attributes;

        Ref<Accessor> indices;

        Ref<Material> material;
    };

    /// \struct SExtension
    /// Extension used for mesh.
    struct SExtension {
        /// \enum EType
        /// Type of extension.
            Unknown
        };

        EType Type; ///< Type of extension.

        /// \fn SExtension
        /// Constructor.
        /// \param [in] pType - type of extension.
        SExtension(const EType pType) :
                Type(pType) {}

        virtual ~SExtension() {
            // empty
        }
    };

    std::list<SExtension *> Extension; ///< List of extensions used in mesh.

    Mesh() {}

    /// Destructor.
    ~Mesh() {
        for (std::list<SExtension *>::iterator it = Extension.begin(), it_end = Extension.end(); it != it_end; it++) {
            delete *it;
        };
    }

    /// @brief Get mesh data from JSON-object and place them to root asset.
    /// \param [in] pJSON_Object - reference to pJSON-object from which data are read.
    /// \param [out] pAsset_Root - reference to root asset where data will be stored.
    void Read(Value &pJSON_Object, Asset &pAsset_Root);
};

struct Skin : public Object {
    Nullable<mat4> bindShapeMatrix; //!< Floating-point 4x4 transformation matrix stored in column-major order.
    Ref<Accessor> inverseBindMatrices; //!< The ID of the accessor containing the floating-point 4x4 inverse-bind matrices.
    std::vector<Ref<Node>> jointNames; //!< Joint names of the joints (nodes with a jointName property) in this skin.
    std::string name; //!< The user-defined name of this object.

    Skin() = default;
    void Read(Value &obj, Asset &r);
};


//
// BVH Asset class
//

//! Root object for a BVH asset
class Asset {
    using IdMap = std::bvh_unordered_map<std::string, int>;

    friend struct Buffer; // To access OpenFile
    friend class AssetWriter;

private:
    IOSystem *mIOSystem;

    std::string mCurrentAssetDir;

    size_t mSceneLength;
    size_t mBodyOffset, mBodyLength;

    std::vector<LazyDictBase *> mDicts;

    IdMap mUsedIds;

    Ref<Buffer> mBodyBuffer;

    Asset(Asset &);
    Asset &operator=(const Asset &);

public:

    AssetMetadata asset;

    Ref<Scene> scene;

public:
    Asset(IOSystem *io = 0) :
            mIOSystem(io), 
            asset(), 
        memset(&extensionsUsed, 0, sizeof(extensionsUsed));
    }

    //! Main function
    void Load(const std::string &file, bool isBinary = false);

    //! Search for an available name, starting from the given strings
    std::string FindUniqueID(const std::string &str, const char *suffix);

    Ref<Buffer> GetBodyBuffer() { return mBodyBuffer; }

private:
    IOStream *OpenFile(const std::string &path, const char *mode, bool absolute = false);
};

} // namespace BVH

// Include the implementation of the methods
#include "BVHAsset.inl"

#endif // ASSIMP_BUILD_NO_BVH_IMPORTER

#endif // BVHASSET_H_INC
