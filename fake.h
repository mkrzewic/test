
#include "memory"
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

enum class byte : unsigned char {};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FakeMQ
////////////////////////////////////////////////////////////////////////////////////////////////////
//__________________________________________________________________________________________________
class FairMQMessage {
  size_t bytes{ 0 };
  byte* data{ nullptr };
  size_t usedBytes{ 0 };

public:
  FairMQMessage(size_t size) : bytes{ size }, data{ new byte[size] }
  {
    printf("ctor FairMQMessage(%li bytes) at %p, data: %p\n", bytes, this, data);
  }

  ~FairMQMessage()
  {
    std::memset(data, 0, bytes);
    delete[] data;
    printf("dtor FairMQMessage() %li bytes at: %p, data: %p\n", bytes, this, data);
  }

  size_t GetSize() const { return bytes; }
  void* GetData() const { return data; }
  bool SetUsedSize(const size_t size)
  {
    if (size <= bytes) {
      usedBytes = size;
      return true;
    }
    else
      return false;
  }
};

using FairMQMessagePtr = std::unique_ptr<FairMQMessage>;
using fairmq_free_fn = void(void* data, void* hint);

//__________________________________________________________________________________________________
class FairMQTransportFactory {
public:
  FairMQMessagePtr CreateMessage(void* data, size_t size, fairmq_free_fn* ffn, void* hint = nullptr) const
  {
    return std::make_unique<FairMQMessage>(size);
  };
  FairMQMessagePtr CreateMessage(const size_t size) const { return std::make_unique<FairMQMessage>(size); };
};

//__________________________________________________________________________________________________
template <typename ContentT>
struct uninitializedValue {
  union {
    ContentT content;
  };
};

struct elem {
  int content;
  // int more;
  elem() noexcept : content{ 0 } { printf("default ctor elem: %i @%p\n", content, this); }
  elem(int i) noexcept : content{ i } { printf("ctor elem %i @%p\n", i, this); }
  ~elem() { printf("dtor elem %i @%p\n", content, this); }
  elem(const elem& in) noexcept : content{ in.content } { printf("copy ctor elem %i %p -> %p\n", content, &in, this); }
  elem(const elem&& in) noexcept : content{ in.content } { printf("move ctor elem %i %p -> %p\n", content, &in, this); }
  elem& operator=(elem& in) noexcept
  {
    content = in.content;
    printf("copy assign elem %i %p = %p\n", content, this, &in);
    return *this;
  }
  elem& operator=(elem&& in) noexcept
  {
    content = in.content;
    printf("move assign elem %i %p = %p\n", content, this, &in);
    return *this;
  }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
struct BaseHeader {
  bool flagsNextHeader{false};
  uint32_t headerSize{sizeof(BaseHeader)};

  uint32_t size() const noexcept {return headerSize;}
  const byte* data() const noexcept { return reinterpret_cast<const byte*>(this); }
  const BaseHeader* next() const noexcept
  {
    return (flagsNextHeader) ? reinterpret_cast<const BaseHeader*>(reinterpret_cast<const byte*>(this) + headerSize)
                             : nullptr;
  }
  BaseHeader() noexcept : flagsNextHeader{ 0 } { printf("default ctor BaseHeader: %i @%p\n", flagsNextHeader, this); }
  ~BaseHeader() { printf("dtor BaseHeader %i @%p\n", flagsNextHeader, this); }
  BaseHeader(const BaseHeader& in) noexcept : flagsNextHeader{ in.flagsNextHeader }
  {
    printf("copy ctor BaseHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  BaseHeader(const BaseHeader&& in) noexcept : flagsNextHeader{ in.flagsNextHeader }
  {
    printf("move ctor BaseHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  BaseHeader& operator=(BaseHeader& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("copy assign BaseHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
  BaseHeader& operator=(BaseHeader&& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("move assign BaseHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
};

template <typename ContentT>
struct InitWrapper {
};

struct Stack {

  // This is ugly and needs fixing BUT:
  // we need a static deleter for fairmq.
  // TODO: maybe use allocator_traits if custom allocation is desired
  //       the deallocate function is then static.
  //       In the case of special transports is is cheap to copy the header stack into a message, so no problem there.
  //       The copy can be avoided if we construct in place inside a FairMQMessage directly (instead of
  //       allocating a unique_ptr we would hold a FairMQMessage which for the large part has similar semantics).
  //
  using Buffer = std::unique_ptr<byte[]>;
  static std::default_delete<byte[]> sDeleter;
  static void freefn(void* /*data*/, void* hint) { Stack::sDeleter(static_cast<byte*>(hint)); }

  size_t bufferSize;
  Buffer buffer;

  byte* data() const { return buffer.get(); }
  size_t size() const { return bufferSize; }

  /// The magic constructor: takes arbitrary number of arguments and serialized them
  /// into the buffer.
  /// Intended use: produce a temporary via an initializer list.
  /// TODO: maybe add a static_assert requiring first arg to be DataHeader
  /// or maybe even require all these to be derived form BaseHeader
  template <typename... Headers>
  Stack(const Headers&... headers) : bufferSize{ size(headers...) }, buffer{ std::make_unique<byte[]>(bufferSize) }
  {
    printf("Stack ctor\n");
    inject(buffer.get(), headers...);
  }
  Stack() = default;
  Stack(Stack&&) = default;
  Stack(Stack&) = delete;
  Stack& operator=(Stack&) = delete;
  Stack& operator=(Stack&&) = default;

  template <typename T, typename... Args>
  static size_t size(const T& h, const Args... args) noexcept
  {
    return size(h) + size(args...);
  }

private:
  template <typename T>
  static size_t size(const T& h) noexcept
  {
    return h.size();
  }

  template <typename T>
  static byte* inject(byte* here, const T& h) noexcept
  {
    printf("  Stack: injecting from %p-> %p\n",&h, here);
    std::copy(h.data(), h.data() + h.size(), here);
    return here + h.size();
  }

  template <typename T, typename... Args>
  static byte* inject(byte* here, const T& h, const Args... args) noexcept
  {
    auto alsohere = inject(here, h);
    (reinterpret_cast<BaseHeader*>(here))->flagsNextHeader = true;
    return inject(alsohere, args...);
  }
};

