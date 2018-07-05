// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_CONTIGUOUSALLOCATOR_H_
#define FLATBUFFERS_GENERATED_CONTIGUOUSALLOCATOR_H_

#include "flatbuffers/flatbuffers.h"

struct SerializedContiguousAllocator;

struct SerializedContiguousAllocator FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_MAXSIZE = 4,
    VT_BLOCKSIZE = 6,
    VT_CURRENTSIZE = 8,
    VT_DATA = 10
  };
  uint32_t maxSize() const {
    return GetField<uint32_t>(VT_MAXSIZE, 0);
  }
  uint32_t blockSize() const {
    return GetField<uint32_t>(VT_BLOCKSIZE, 0);
  }
  uint32_t currentSize() const {
    return GetField<uint32_t>(VT_CURRENTSIZE, 0);
  }
  const flatbuffers::Vector<int8_t> *data() const {
    return GetPointer<const flatbuffers::Vector<int8_t> *>(VT_DATA);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_MAXSIZE) &&
           VerifyField<uint32_t>(verifier, VT_BLOCKSIZE) &&
           VerifyField<uint32_t>(verifier, VT_CURRENTSIZE) &&
           VerifyOffset(verifier, VT_DATA) &&
           verifier.Verify(data()) &&
           verifier.EndTable();
  }
};

struct SerializedContiguousAllocatorBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_maxSize(uint32_t maxSize) {
    fbb_.AddElement<uint32_t>(SerializedContiguousAllocator::VT_MAXSIZE, maxSize, 0);
  }
  void add_blockSize(uint32_t blockSize) {
    fbb_.AddElement<uint32_t>(SerializedContiguousAllocator::VT_BLOCKSIZE, blockSize, 0);
  }
  void add_currentSize(uint32_t currentSize) {
    fbb_.AddElement<uint32_t>(SerializedContiguousAllocator::VT_CURRENTSIZE, currentSize, 0);
  }
  void add_data(flatbuffers::Offset<flatbuffers::Vector<int8_t>> data) {
    fbb_.AddOffset(SerializedContiguousAllocator::VT_DATA, data);
  }
  explicit SerializedContiguousAllocatorBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  SerializedContiguousAllocatorBuilder &operator=(const SerializedContiguousAllocatorBuilder &);
  flatbuffers::Offset<SerializedContiguousAllocator> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<SerializedContiguousAllocator>(end);
    return o;
  }
};

inline flatbuffers::Offset<SerializedContiguousAllocator> CreateSerializedContiguousAllocator(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t maxSize = 0,
    uint32_t blockSize = 0,
    uint32_t currentSize = 0,
    flatbuffers::Offset<flatbuffers::Vector<int8_t>> data = 0) {
  SerializedContiguousAllocatorBuilder builder_(_fbb);
  builder_.add_data(data);
  builder_.add_currentSize(currentSize);
  builder_.add_blockSize(blockSize);
  builder_.add_maxSize(maxSize);
  return builder_.Finish();
}

inline flatbuffers::Offset<SerializedContiguousAllocator> CreateSerializedContiguousAllocatorDirect(
    flatbuffers::FlatBufferBuilder &_fbb,
    uint32_t maxSize = 0,
    uint32_t blockSize = 0,
    uint32_t currentSize = 0,
    const std::vector<int8_t> *data = nullptr) {
  return CreateSerializedContiguousAllocator(
      _fbb,
      maxSize,
      blockSize,
      currentSize,
      data ? _fbb.CreateVector<int8_t>(*data) : 0);
}

#endif  // FLATBUFFERS_GENERATED_CONTIGUOUSALLOCATOR_H_
