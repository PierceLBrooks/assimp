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

#include <assimp/MemoryIOWrapper.h>
#include <assimp/StringUtils.h>
#include <iomanip>

// Header files, Assimp
#include <assimp/DefaultLogger.hpp>
#include <assimp/Base64.hpp>

using namespace Assimp;

namespace BVH {
using namespace BVHCommon;

#if _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4706)
#endif // _MSC_VER

//
// LazyDict methods
//

template <class T>
inline LazyDict<T>::LazyDict(Asset &asset, const char *dictId, const char *extId) :
        mDictId(dictId), mExtId(extId), mDict(0), mAsset(asset) {
    asset.mDicts.push_back(this); // register to the list of dictionaries
}

template <class T>
inline LazyDict<T>::~LazyDict() {
    for (size_t i = 0; i < mObjs.size(); ++i) {
        delete mObjs[i];
    }
}

template <class T>
inline void LazyDict<T>::AttachToDocument(Document &doc) {
    Value *container = 0;

    if (mExtId) {
        if (Value *exts = FindObject(doc, "extensions")) {
            container = FindObject(*exts, mExtId);
        }
    } else {
        container = &doc;
    }

    if (container) {
        mDict = FindObject(*container, mDictId);
    }
}

template <class T>
inline void LazyDict<T>::DetachFromDocument() {
    mDict = 0;
}

template <class T>
Ref<T> LazyDict<T>::Get(unsigned int i) {
    return Ref<T>(mObjs, i);
}

template <class T>
Ref<T> LazyDict<T>::Get(const char *id) {
    id = T::TranslateId(mAsset, id);

    typename Dict::iterator it = mObjsById.find(id);
    if (it != mObjsById.end()) { // already created?
        return Ref<T>(mObjs, it->second);
    }

    // read it from the JSON object
    if (!mDict) {
        throw DeadlyImportError("BVH: Missing section \"", mDictId, "\"");
    }

    Value::MemberIterator obj = mDict->FindMember(id);
    if (obj == mDict->MemberEnd()) {
        throw DeadlyImportError("BVH: Missing object with id \"", id, "\" in \"", mDictId, "\"");
    }
    if (!obj->value.IsObject()) {
        throw DeadlyImportError("BVH: Object with id \"", id, "\" is not a JSON object");
    }

    // create an instance of the given type
    T *inst = new T();
    inst->id = id;
    ReadMember(obj->value, "name", inst->name);
    inst->Read(obj->value, mAsset);
    return Add(inst);
}

template <class T>
Ref<T> LazyDict<T>::Add(T *obj) {
    unsigned int idx = unsigned(mObjs.size());
    mObjs.push_back(obj);
    mObjsById[obj->id] = idx;
    mAsset.mUsedIds[obj->id] = true;
    return Ref<T>(mObjs, idx);
}

template <class T>
Ref<T> LazyDict<T>::Create(const char *id) {
    Asset::IdMap::iterator it = mAsset.mUsedIds.find(id);
    if (it != mAsset.mUsedIds.end()) {
        throw DeadlyImportError("BVH: two objects with the same ID exist");
    }
    T *inst = new T();
    inst->id = id;
    return Add(inst);
}

//
// BVH dictionary objects methods
//

inline Buffer::Buffer() :
        byteLength(0), type(Type_arraybuffer), EncodedRegion_Current(nullptr), mIsSpecial(false) {}

inline Buffer::~Buffer() {
    for (SEncodedRegion *reg : EncodedRegion_List)
        delete reg;
}

inline const char *Buffer::TranslateId(Asset &r, const char *id) {
    return id;
}

inline void Buffer::Read(Value &obj, Asset &r) {
    size_t statedLength = MemberOrDefault<size_t>(obj, "byteLength", 0);
    byteLength = statedLength;

    Value *it = FindString(obj, "uri");
    if (!it) {
        if (statedLength > 0) {
            throw DeadlyImportError("BVH: buffer with non-zero length missing the \"uri\" attribute");
        }
        return;
    }

    const char *uri = it->GetString();

    BVHCommon::Util::DataURI dataURI;
    if (ParseDataURI(uri, it->GetStringLength(), dataURI)) {
        if (dataURI.base64) {
            uint8_t *data = 0;
            this->byteLength = Base64::Decode(dataURI.data, dataURI.dataLength, data);
            this->mData.reset(data, std::default_delete<uint8_t[]>());

            if (statedLength > 0 && this->byteLength != statedLength) {
                throw DeadlyImportError("BVH: buffer \"", id, "\", expected ", ai_to_string(statedLength),
                        " bytes, but found ", ai_to_string(dataURI.dataLength));
            }
        } else { // assume raw data
            if (statedLength != dataURI.dataLength) {
                throw DeadlyImportError("BVH: buffer \"", id, "\", expected ", ai_to_string(statedLength),
                        " bytes, but found ", ai_to_string(dataURI.dataLength));
            }

            this->mData.reset(new uint8_t[dataURI.dataLength], std::default_delete<uint8_t[]>());
            memcpy(this->mData.get(), dataURI.data, dataURI.dataLength);
        }
    } else { // Local file
        if (byteLength > 0) {
            std::string dir = !r.mCurrentAssetDir.empty() ? (
                                                                    r.mCurrentAssetDir.back() == '/' ?
                                                                            r.mCurrentAssetDir :
                                                                            r.mCurrentAssetDir + '/') :
                                                            "";

            IOStream *file = r.OpenFile(dir + uri, "rb");
            if (file) {
                bool ok = LoadFromStream(*file, byteLength);
                delete file;

                if (!ok)
                    throw DeadlyImportError("BVH: error while reading referenced file \"", uri, "\"");
            } else {
                throw DeadlyImportError("BVH: could not open referenced file \"", uri, "\"");
            }
        }
    }
}

inline bool Buffer::LoadFromStream(IOStream &stream, size_t length, size_t baseOffset) {
    byteLength = length ? length : stream.FileSize();

    if (baseOffset) {
        stream.Seek(baseOffset, aiOrigin_SET);
    }

    mData.reset(new uint8_t[byteLength], std::default_delete<uint8_t[]>());

    if (stream.Read(mData.get(), byteLength, 1) != 1) {
        return false;
    }
    return true;
}

inline void Buffer::EncodedRegion_Mark(const size_t pOffset, const size_t pEncodedData_Length, uint8_t *pDecodedData, const size_t pDecodedData_Length, const std::string &pID) {
    // Check pointer to data
    if (pDecodedData == nullptr) throw DeadlyImportError("BVH: for marking encoded region pointer to decoded data must be provided.");

    // Check offset
    if (pOffset > byteLength) {
        const uint8_t val_size = 32;

        char val[val_size];

        ai_snprintf(val, val_size, AI_SIZEFMT, pOffset);
        throw DeadlyImportError("BVH: incorrect offset value (", val, ") for marking encoded region.");
    }

    // Check length
    if ((pOffset + pEncodedData_Length) > byteLength) {
        const uint8_t val_size = 64;

        char val[val_size];

        ai_snprintf(val, val_size, AI_SIZEFMT "/" AI_SIZEFMT, pOffset, pEncodedData_Length);
        throw DeadlyImportError("BVH: encoded region with offset/length (", val, ") is out of range.");
    }

    // Add new region
    EncodedRegion_List.push_back(new SEncodedRegion(pOffset, pEncodedData_Length, pDecodedData, pDecodedData_Length, pID));
    // And set new value for "byteLength"
    byteLength += (pDecodedData_Length - pEncodedData_Length);
}

inline void Buffer::EncodedRegion_SetCurrent(const std::string &pID) {
    if ((EncodedRegion_Current != nullptr) && (EncodedRegion_Current->ID == pID)) return;

    for (SEncodedRegion *reg : EncodedRegion_List) {
        if (reg->ID == pID) {
            EncodedRegion_Current = reg;

            return;
        }
    }

    throw DeadlyImportError("BVH: EncodedRegion with ID: \"", pID, "\" not found.");
}

inline bool Buffer::ReplaceData(const size_t pBufferData_Offset, const size_t pBufferData_Count, const uint8_t *pReplace_Data, const size_t pReplace_Count) {
    const size_t new_data_size = byteLength + pReplace_Count - pBufferData_Count;

    uint8_t *new_data;

    if ((pBufferData_Count == 0) || (pReplace_Count == 0) || (pReplace_Data == nullptr)) return false;

    new_data = new uint8_t[new_data_size];
    // Copy data which place before replacing part.
    memcpy(new_data, mData.get(), pBufferData_Offset);
    // Copy new data.
    memcpy(&new_data[pBufferData_Offset], pReplace_Data, pReplace_Count);
    // Copy data which place after replacing part.
    memcpy(&new_data[pBufferData_Offset + pReplace_Count], &mData.get()[pBufferData_Offset + pBufferData_Count], pBufferData_Offset);
    // Apply new data
    mData.reset(new_data, std::default_delete<uint8_t[]>());
    byteLength = new_data_size;

    return true;
}

inline size_t Buffer::AppendData(uint8_t *data, size_t length) {
    size_t offset = this->byteLength;
    Grow(length);
    memcpy(mData.get() + offset, data, length);
    return offset;
}

inline void Buffer::Grow(size_t amount) {
    if (amount <= 0) return;
    if (capacity >= byteLength + amount) {
        byteLength += amount;
        return;
    }

    // Shift operation is standard way to divide integer by 2, it doesn't cast it to float back and forth, also works for odd numbers,
    // originally it would look like: static_cast<size_t>(capacity * 1.5f)
    capacity = std::max(capacity + (capacity >> 1), byteLength + amount);

    uint8_t *b = new uint8_t[capacity];
    if (mData) memcpy(b, mData.get(), byteLength);
    mData.reset(b, std::default_delete<uint8_t[]>());
    byteLength += amount;
}

//
// struct BufferView
//

inline void BufferView::Read(Value &obj, Asset &r) {
    const char *bufferId = MemberOrDefault<const char *>(obj, "buffer", 0);
    if (bufferId) {
        buffer = r.buffers.Get(bufferId);
    }

    byteOffset = MemberOrDefault(obj, "byteOffset", 0u);
    byteLength = MemberOrDefault(obj, "byteLength", 0u);
}

//
// struct Accessor
//

inline void Accessor::Read(Value &obj, Asset &r) {
    const char *bufferViewId = MemberOrDefault<const char *>(obj, "bufferView", 0);
    if (bufferViewId) {
        bufferView = r.bufferViews.Get(bufferViewId);
    }

    byteOffset = MemberOrDefault(obj, "byteOffset", 0u);
    byteStride = MemberOrDefault(obj, "byteStride", 0u);
    componentType = MemberOrDefault(obj, "componentType", ComponentType_BYTE);
    count = MemberOrDefault(obj, "count", 0u);

    const char *typestr;
    type = ReadMember(obj, "type", typestr) ? AttribType::FromString(typestr) : AttribType::SCALAR;
}

inline unsigned int Accessor::GetNumComponents() {
    return AttribType::GetNumComponents(type);
}

inline unsigned int Accessor::GetBytesPerComponent() {
    return int(ComponentTypeSize(componentType));
}

inline unsigned int Accessor::GetElementSize() {
    return GetNumComponents() * GetBytesPerComponent();
}

inline uint8_t *Accessor::GetPointer() {
    if (!bufferView || !bufferView->buffer) return 0;
    uint8_t *basePtr = bufferView->buffer->GetPointer();
    if (!basePtr) return 0;

    size_t offset = byteOffset + bufferView->byteOffset;

    // Check if region is encoded.
    if (bufferView->buffer->EncodedRegion_Current != nullptr) {
        const size_t begin = bufferView->buffer->EncodedRegion_Current->Offset;
        const size_t end = begin + bufferView->buffer->EncodedRegion_Current->DecodedData_Length;

        if ((offset >= begin) && (offset < end))
            return &bufferView->buffer->EncodedRegion_Current->DecodedData[offset - begin];
    }

    return basePtr + offset;
}

namespace {
inline void CopyData(size_t count,
        const uint8_t *src, size_t src_stride,
        uint8_t *dst, size_t dst_stride) {
    if (src_stride == dst_stride) {
        memcpy(dst, src, count * src_stride);
    } else {
        size_t sz = std::min(src_stride, dst_stride);
        for (size_t i = 0; i < count; ++i) {
            memcpy(dst, src, sz);
            if (sz < dst_stride) {
                memset(dst + sz, 0, dst_stride - sz);
            }
            src += src_stride;
            dst += dst_stride;
        }
    }
}
} // namespace

template <class T>
bool Accessor::ExtractData(T *&outData) {
    uint8_t *data = GetPointer();
    if (!data) return false;

    const size_t elemSize = GetElementSize();
    const size_t totalSize = elemSize * count;

    const size_t stride = byteStride ? byteStride : elemSize;

    const size_t targetElemSize = sizeof(T);
    ai_assert(elemSize <= targetElemSize);

    ai_assert(count * stride <= bufferView->byteLength);

    outData = new T[count];
    if (stride == elemSize && targetElemSize == elemSize) {
        memcpy(outData, data, totalSize);
    } else {
        for (size_t i = 0; i < count; ++i) {
            memcpy(outData + i, data + i * stride, elemSize);
        }
    }

    return true;
}

inline void Accessor::WriteData(size_t cnt, const void *src_buffer, size_t src_stride) {
    uint8_t *buffer_ptr = bufferView->buffer->GetPointer();
    size_t offset = byteOffset + bufferView->byteOffset;

    size_t dst_stride = GetNumComponents() * GetBytesPerComponent();

    const uint8_t *src = reinterpret_cast<const uint8_t *>(src_buffer);
    uint8_t *dst = reinterpret_cast<uint8_t *>(buffer_ptr + offset);

    ai_assert(dst + count * dst_stride <= buffer_ptr + bufferView->buffer->byteLength);
    CopyData(cnt, src, src_stride, dst, dst_stride);
}

inline Accessor::Indexer::Indexer(Accessor &acc) :
        accessor(acc), data(acc.GetPointer()), elemSize(acc.GetElementSize()), stride(acc.byteStride ? acc.byteStride : elemSize) {
}

//! Accesses the i-th value as defined by the accessor
template <class T>
T Accessor::Indexer::GetValue(int i) {
    ai_assert(data);
    ai_assert(i * stride < accessor.bufferView->byteLength);
    T value = T();
    memcpy(&value, data + i * stride, elemSize);
    //value >>= 8 * (sizeof(T) - elemSize);
    return value;
}

namespace {
void SetVector(vec4 &v, float x, float y, float z, float w) {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
}
} // namespace

namespace {

template <int N>
inline int Compare(const char *attr, const char (&str)[N]) {
    return (strncmp(attr, str, N - 1) == 0) ? N - 1 : 0;
}

inline bool GetAttribVector(Mesh::Primitive &p, const char *attr, Mesh::AccessorList *&v, int &pos) {
    if ((pos = Compare(attr, "POSITION"))) {
        v = &(p.attributes.position);
    } else if ((pos = Compare(attr, "NORMAL"))) {
        v = &(p.attributes.normal);
    } else if ((pos = Compare(attr, "TEXCOORD"))) {
        v = &(p.attributes.texcoord);
    } else if ((pos = Compare(attr, "COLOR"))) {
        v = &(p.attributes.color);
    } else if ((pos = Compare(attr, "JOINT"))) {
        v = &(p.attributes.joint);
    } else if ((pos = Compare(attr, "JOINTMATRIX"))) {
        v = &(p.attributes.jointmatrix);
    } else if ((pos = Compare(attr, "WEIGHT"))) {
        v = &(p.attributes.weight);
    } else
        return false;
    return true;
}
} // namespace

inline void Mesh::Read(Value &pJSON_Object, Asset &pAsset_Root) {
    /****************** Mesh primitives ******************/
    Value *curPrimitives = FindArray(pJSON_Object, "primitives");
    if (nullptr != curPrimitives) {
        this->primitives.resize(curPrimitives->Size());
        for (unsigned int i = 0; i < curPrimitives->Size(); ++i) {
            Value &primitive = (*curPrimitives)[i];

            Primitive &prim = this->primitives[i];
            prim.mode = MemberOrDefault(primitive, "mode", PrimitiveMode_TRIANGLES);

            if (Value *attrs = FindObject(primitive, "attributes")) {
                for (Value::MemberIterator it = attrs->MemberBegin(); it != attrs->MemberEnd(); ++it) {
                    if (!it->value.IsString()) continue;
                    const char *attr = it->name.GetString();
                    // Valid attribute semantics include POSITION, NORMAL, TEXCOORD, COLOR, JOINT, JOINTMATRIX,
                    // and WEIGHT.Attribute semantics can be of the form[semantic]_[set_index], e.g., TEXCOORD_0, TEXCOORD_1, etc.

                    int undPos = 0;
                    Mesh::AccessorList *vec = 0;
                    if (GetAttribVector(prim, attr, vec, undPos)) {
                        size_t idx = (attr[undPos] == '_') ? atoi(attr + undPos + 1) : 0;
                        if ((*vec).size() <= idx) (*vec).resize(idx + 1);
                        (*vec)[idx] = pAsset_Root.accessors.Get(it->value.GetString());
                    }
                }
            }

            if (Value *indices = FindString(primitive, "indices")) {
                prim.indices = pAsset_Root.accessors.Get(indices->GetString());
            }

            if (Value *material = FindString(primitive, "material")) {
                prim.material = pAsset_Root.materials.Get(material->GetString());
            }
        }
    }

    return; // After label some operators must be present.
}

inline void Asset::Load(const std::string &pFile, bool isBinary) {
    mCurrentAssetDir.clear();

    /*int pos = std::max(int(pFile.rfind('/')), int(pFile.rfind('\\')));
    if (pos != int(std::string::npos)) mCurrentAssetDir = pFile.substr(0, pos + 1);*/
    if (0 != strncmp(pFile.c_str(), AI_MEMORYIO_MAGIC_FILENAME, AI_MEMORYIO_MAGIC_FILENAME_LENGTH)) {
        mCurrentAssetDir = getCurrentAssetDir(pFile);
    }

    shared_ptr<IOStream> stream(OpenFile(pFile.c_str(), "rb", true));
    if (!stream) {
        throw DeadlyImportError("BVH: Could not open file for reading");
    }

    // is binary? then read the header
    if (isBinary) {
        SetAsBinary(); // also creates the body buffer
        ReadBinaryHeader(*stream);
    } else {
        mSceneLength = stream->FileSize();
        mBodyLength = 0;
    }

    // Smallest legal JSON file is "{}" Smallest loadable BVH file is larger than that but catch it later
    if (mSceneLength < 2) {
        throw DeadlyImportError("BVH: No JSON file contents");
    }

    // Binary format only supports up to 4GB of JSON so limit it there to avoid extreme memory allocation
    if (mSceneLength >= std::numeric_limits<uint32_t>::max()) {
        throw DeadlyImportError("BVH: JSON size greater than 4GB");
    }

    // read the scene data, ensure null termination
    std::vector<char> sceneData(mSceneLength + 1);
    sceneData[mSceneLength] = '\0';

    if (stream->Read(&sceneData[0], 1, mSceneLength) != mSceneLength) {
        throw DeadlyImportError("BVH: Could not read the file contents");
    }

    // parse the JSON document

    Document doc;
    doc.ParseInsitu(&sceneData[0]);

    if (doc.HasParseError()) {
        char buffer[32];
        ai_snprintf(buffer, 32, "%d", static_cast<int>(doc.GetErrorOffset()));
        throw DeadlyImportError("BVH: JSON parse error, offset ", buffer, ": ", GetParseError_En(doc.GetParseError()));
    }

    if (!doc.IsObject()) {
        throw DeadlyImportError("BVH: JSON document root must be a JSON object");
    }

    // Fill the buffer instance for the current file embedded contents
    if (mBodyLength > 0) {
        if (!mBodyBuffer->LoadFromStream(*stream, mBodyLength, mBodyOffset)) {
            throw DeadlyImportError("BVH: Unable to read bvh file");
        }
    }

    // Load the metadata
    asset.Read(doc);
    if (!asset) {
        return;
    }

    ReadExtensionsUsed(doc);

    // Prepare the dictionaries
    for (size_t i = 0; i < mDicts.size(); ++i) {
        mDicts[i]->AttachToDocument(doc);
    }

    // Read the "scene" property, which specifies which scene to load
    // and recursively load everything referenced by it
    Value *curScene = FindString(doc, "scene");
    if (nullptr != curScene) {
        this->scene = scenes.Get(curScene->GetString());
    }

    // Clean up
    for (size_t i = 0; i < mDicts.size(); ++i) {
        mDicts[i]->DetachFromDocument();
    }
}

inline IOStream *Asset::OpenFile(const std::string &path, const char *mode, bool absolute) {
#ifdef ASSIMP_API
    (void)absolute;
    return mIOSystem->Open(path, mode);
#else
    if (path.size() < 2) return 0;
    if (!absolute && path[1] != ':' && path[0] != '/') { // relative?
        path = mCurrentAssetDir + path;
    }
    FILE *f = fopen(path.c_str(), mode);
    return f ? new IOStream(f) : 0;
#endif
}

inline std::string Asset::FindUniqueID(const std::string &str, const char *suffix) {
    std::string id = str;

    if (!id.empty()) {
        if (mUsedIds.find(id) == mUsedIds.end())
            return id;

        id += "_";
    }

    id += suffix;

    Asset::IdMap::iterator it = mUsedIds.find(id);
    if (it == mUsedIds.end())
        return id;

    char buffer[1024];
    int offset = ai_snprintf(buffer, sizeof(buffer), "%s_", id.c_str());
    for (int i = 0; it != mUsedIds.end(); ++i) {
        ai_snprintf(buffer + offset, sizeof(buffer) - offset, "%d", i);
        id = buffer;
        it = mUsedIds.find(id);
    }

    return id;
}

#if _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

} // namespace BVH
