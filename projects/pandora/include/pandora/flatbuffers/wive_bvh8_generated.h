// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_WIVEBVH8_PANDORA_H_
#define FLATBUFFERS_GENERATED_WIVEBVH8_PANDORA_H_

#include "flatbuffers/flatbuffers.h"

#include "contiguous_allocator_generated.h"

namespace pandora {

struct SerializedWiveBVH8;

struct SerializedWiveBVH8 FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_INNERNODEALLOCATOR = 4,
    VT_LEAFNODEALLOCATOR = 6,
    VT_COMPRESSEDROOTHANDLE = 8,
    VT_NUMLEAFOBJECTS = 10
  };
  const SerializedContiguousAllocator *innerNodeAllocator() const {
    return GetPointer<const SerializedContiguousAllocator *>(VT_INNERNODEALLOCATOR);
  }
  const SerializedContiguousAllocator *leafNodeAllocator() const {
    return GetPointer<const SerializedContiguousAllocator *>(VT_LEAFNODEALLOCATOR);
  }
  uint32_t compressedRootHandle() const {
    return GetField<uint32_t>(VT_COMPRESSEDROOTHANDLE, 0);
  }
  uint32_t numLeafObjects() const {
    return GetField<uint32_t>(VT_NUMLEAFOBJECTS, 0);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_INNERNODEALLOCATOR) &&
           verifier.VerifyTable(innerNodeAllocator()) &&
           VerifyOffset(verifier, VT_LEAFNODEALLOCATOR) &&
           verifier.VerifyTable(leafNodeAllocator()) &&
           VerifyField<uint32_t>(verifier, VT_COMPRESSEDROOTHANDLE) &&
           VerifyField<uint32_t>(verifier, VT_NUMLEAFOBJECTS) &&
           verifier.EndTable();
  }
};

struct SerializedWiveBVH8Builder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_innerNodeAllocator(flatbuffers::Offset<SerializedContiguousAllocator> innerNodeAllocator) {
    fbb_.AddOffset(SerializedWiveBVH8::VT_INNERNODEALLOCATOR, innerNodeAllocator);
  }
  void add_leafNodeAllocator(flatbuffers::Offset<SerializedContiguousAllocator> leafNodeAllocator) {
    fbb_.AddOffset(SerializedWiveBVH8::VT_LEAFNODEALLOCATOR, leafNodeAllocator);
  }
  void add_compressedRootHandle(uint32_t compressedRootHandle) {
    fbb_.AddElement<uint32_t>(SerializedWiveBVH8::VT_COMPRESSEDROOTHANDLE, compressedRootHandle, 0);
  }
  void add_numLeafObjects(uint32_t numLeafObjects) {
    fbb_.AddElement<uint32_t>(SerializedWiveBVH8::VT_NUMLEAFOBJECTS, numLeafObjects, 0);
  }
  explicit SerializedWiveBVH8Builder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  SerializedWiveBVH8Builder &operator=(const SerializedWiveBVH8Builder &);
  flatbuffers::Offset<SerializedWiveBVH8> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<SerializedWiveBVH8>(end);
    return o;
  }
};

inline flatbuffers::Offset<SerializedWiveBVH8> CreateSerializedWiveBVH8(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<SerializedContiguousAllocator> innerNodeAllocator = 0,
    flatbuffers::Offset<SerializedContiguousAllocator> leafNodeAllocator = 0,
    uint32_t compressedRootHandle = 0,
    uint32_t numLeafObjects = 0) {
  SerializedWiveBVH8Builder builder_(_fbb);
  builder_.add_numLeafObjects(numLeafObjects);
  builder_.add_compressedRootHandle(compressedRootHandle);
  builder_.add_leafNodeAllocator(leafNodeAllocator);
  builder_.add_innerNodeAllocator(innerNodeAllocator);
  return builder_.Finish();
}

inline const pandora::SerializedWiveBVH8 *GetSerializedWiveBVH8(const void *buf) {
  return flatbuffers::GetRoot<pandora::SerializedWiveBVH8>(buf);
}

inline const pandora::SerializedWiveBVH8 *GetSizePrefixedSerializedWiveBVH8(const void *buf) {
  return flatbuffers::GetSizePrefixedRoot<pandora::SerializedWiveBVH8>(buf);
}

inline bool VerifySerializedWiveBVH8Buffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<pandora::SerializedWiveBVH8>(nullptr);
}

inline bool VerifySizePrefixedSerializedWiveBVH8Buffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<pandora::SerializedWiveBVH8>(nullptr);
}

inline void FinishSerializedWiveBVH8Buffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<pandora::SerializedWiveBVH8> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedSerializedWiveBVH8Buffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<pandora::SerializedWiveBVH8> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace pandora

#endif  // FLATBUFFERS_GENERATED_WIVEBVH8_PANDORA_H_
