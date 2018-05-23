
#include "memory"
#include <boost/container/pmr/global_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

enum class byte : unsigned char {};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FakeMQ
////////////////////////////////////////////////////////////////////////////////////////////////////
//__________________________________________________________________________________________________

using fairmq_free_fn = void(void* data, void* hint);

class FairMQMessage {
  fairmq_free_fn* freefn{ nullptr };
  void* hint{ nullptr };
  size_t bytes{ 0 };
  byte* data{ nullptr };
  size_t usedBytes{ 0 };

public:
  FairMQMessage(size_t size) : bytes{ size }, data{ new byte[size] }
  {
    printf("ctor FairMQMessage(%li bytes) at %p, data: %p\n", bytes, this, data);
  }

  FairMQMessage(void* data_, size_t size, fairmq_free_fn* ffn = nullptr, void* hint_ = nullptr)
    : freefn{ ffn }, hint{ hint_ }, bytes{ size }, data{ static_cast<byte*>(data_) }
  {
    printf("ctor FairMQMessage(%li bytes) at %p, data: %p\n", bytes, this, data);
  }

  ~FairMQMessage()
  {
    if (freefn) {
      printf("ffn FairMQMessage() %li bytes at: %p, data: %p, freefn: %p, hint: %p\n", bytes, this, data, freefn, hint);
      freefn(data, hint);
    }
    else {
      printf("dtor FairMQMessage() %li bytes at: %p, data: %p\n", bytes, this, data);
      std::memset(data, 0, bytes);
      delete[] data;
    }
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

//__________________________________________________________________________________________________
class FairMQTransportFactory {
public:
  FairMQMessagePtr CreateMessage(void* data, size_t size, fairmq_free_fn* ffn, void* hint = nullptr) const
  {
    return std::make_unique<FairMQMessage>(data, size, ffn, hint);
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

class FairMQParts {
private:
  using container = std::vector<std::unique_ptr<FairMQMessage>>;

public:
  /// Default constructor
  FairMQParts() : fParts(){};
  /// Copy Constructor
  FairMQParts(const FairMQParts&) = delete;
  /// Move constructor
  FairMQParts(FairMQParts&& p) = default;
  /// Assignment operator
  FairMQParts& operator=(const FairMQParts&) = delete;
  /// Default destructor
  ~FairMQParts(){};

  /// Adds part (FairMQMessage) to the container
  /// @param msg message pointer (for example created with NewMessage() method of FairMQDevice)
  void AddPart(FairMQMessage* msg) { fParts.push_back(std::unique_ptr<FairMQMessage>(msg)); }

  /// Adds part (std::unique_ptr<FairMQMessage>&) to the container (move)
  /// @param msg unique pointer to FairMQMessage
  /// lvalue ref (move not required when passing argument)
  // inline void AddPart(std::unique_ptr<FairMQMessage>& msg)
  // {
  //     fParts.push_back(std::move(msg));
  // }

  /// Adds part (std::unique_ptr<FairMQMessage>&) to the container (move)
  /// @param msg unique pointer to FairMQMessage
  /// rvalue ref (move required when passing argument)
  void AddPart(std::unique_ptr<FairMQMessage>&& msg) { fParts.push_back(std::move(msg)); }

  /// Get reference to part in the container at index (without bounds check)
  /// @param index container index
  FairMQMessage& operator[](const int index) { return *(fParts[index]); }

  /// Get reference to unique pointer to part in the container at index (with bounds check)
  /// @param index container index
  std::unique_ptr<FairMQMessage>& At(const int index) { return fParts.at(index); }

  // ref version
  FairMQMessage& AtRef(const int index) { return *(fParts.at(index)); }

  /// Get number of parts in the container
  /// @return number of parts in the container
  int Size() const { return fParts.size(); }

  container fParts;

  // forward container iterators
  using iterator = container::iterator;
  using const_iterator = container::const_iterator;
  auto begin() -> decltype(fParts.begin()) { return fParts.begin(); }
  auto end() -> decltype(fParts.end()) { return fParts.end(); }
  auto cbegin() -> decltype(fParts.cbegin()) { return fParts.cbegin(); }
  auto cend() -> decltype(fParts.cend()) { return fParts.cend(); }
};

struct BaseHeader {
  char magic[4]{ 'O', '2', 'O', '2' };
  uint64_t alignment{ 0 };
  bool flagsNextHeader{ false };
  uint32_t headerSize{ sizeof(BaseHeader) };

  constexpr uint32_t size() const noexcept { return headerSize; }
  const byte* data() const noexcept { return reinterpret_cast<const byte*>(this); }
  const BaseHeader* next() const noexcept
  {
    return (flagsNextHeader) ? reinterpret_cast<const BaseHeader*>(reinterpret_cast<const byte*>(this) + headerSize)
                             : nullptr;
  }
  constexpr BaseHeader() noexcept : flagsNextHeader{ 0 }
  {
    printf("default ctor BaseHeader: %i @%p, size: %u\n", flagsNextHeader, this, headerSize);
  }
  constexpr BaseHeader(uint32_t size) noexcept : flagsNextHeader{ 0 }, headerSize{ size }
  {
    printf("default ctor BaseHeader: %i @%p, size: %u\n", flagsNextHeader, this, size);
  }
  //~BaseHeader() { printf("dtor BaseHeader %i @%p\n", flagsNextHeader, this); }
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

using O2Message = FairMQParts;

struct DataHeader : public BaseHeader {
  char contents[3]{ 'a', 'b', 'c' };
  constexpr DataHeader() noexcept : BaseHeader{ sizeof(DataHeader) }
  {
    printf("default ctor DataHeader: %i @%p\n", flagsNextHeader, this);
  }
  //~DataHeader() { printf("dtor DataHeader %i @%p\n", flagsNextHeader, this); }
  DataHeader(const DataHeader& in) noexcept
  {
    printf("copy ctor DataHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  DataHeader(const DataHeader&& in) noexcept
  {
    printf("move ctor DataHeader %i %p -> %p\n", flagsNextHeader, &in, this);
  }
  DataHeader& operator=(DataHeader& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("copy assign DataHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
  DataHeader& operator=(DataHeader&& in) noexcept
  {
    flagsNextHeader = in.flagsNextHeader;
    printf("move assign DataHeader %i %p = %p\n", flagsNextHeader, this, &in);
    return *this;
  }
};

struct Stack {

  static void freefn(void* data, void* hint)
  {
    printf("Stack::freefn() data: %p, hint: %p\n", data, hint);
    boost::container::pmr::memory_resource* resource = static_cast<boost::container::pmr::memory_resource*>(hint);
    resource->deallocate(data, 0, 0);
  }

  struct freeobj {
    boost::container::pmr::memory_resource* resource{ nullptr };
    void operator()(byte* ptr) { Stack::freefn(ptr, resource); }
  };

  using allocator_type = boost::container::pmr::polymorphic_allocator<byte>;
  using BufferType = std::unique_ptr<byte[], freeobj>;
  using value_type = byte;

  Stack() = default;
  Stack(Stack&&) = default;
  Stack(Stack&) = delete;
  Stack& operator=(Stack&) = delete;
  Stack& operator=(Stack&&) = default;

  byte* data() const { return buffer.get(); }
  size_t size() const { return bufferSize; }
  void release() { buffer.release(); }
  void clear() { buffer.release(); }
  allocator_type get_allocator() const { return allocator; }

  /// The magic constructor: takes arbitrary number of arguments and serialized them
  /// into the buffer.
  /// Intended use: produce a temporary via an initializer list.
  /// TODO: maybe add a static_assert requiring first arg to be DataHeader
  /// or maybe even require all these to be derived form BaseHeader
  template <typename... Headers>
  Stack(const BaseHeader& firstHeader, Headers&&... headers)
    : Stack(boost::container::pmr::new_delete_resource(), std::forward<Headers>(headers)...)
  {
  }

  template <typename... Headers>
  Stack(const allocator_type allocator_, const DataHeader& firstHeader, Headers&&... headers)
    : allocator{ allocator_ },
      bufferSize{ size(firstHeader) + size(std::forward<Headers>(headers)...) },
      buffer{ static_cast<byte*>(allocator.resource()->allocate(bufferSize, alignof(std::max_align_t))),
              freeobj{ allocator.resource() } }
  {
    printf("Stack ctor, size %li\n", bufferSize);
    inject(inject(buffer.get(), firstHeader), std::forward<Headers>(headers)...);
  }

  template <typename T, typename... Args>
  static size_t size(T&& h, Args&&... args) noexcept
  {
    return size(std::forward<T>(h)) + size(std::forward<Args>(args)...);
  }

private:
  allocator_type allocator{ boost::container::pmr::new_delete_resource() };
  size_t bufferSize{ 0 };
  BufferType buffer{ nullptr };

  template <typename T>
  static size_t size(T&& h) noexcept
  {
    return h.size();
  }

  template <typename T>
  static byte* inject(byte* here, T&& h) noexcept
  {
    printf("  Stack: injecting header from %p-> %p\n", &h, here);
    std::copy(h.data(), h.data() + h.size(), here);
    // new (here) T(h);
    return here + h.size();
  }

  template <typename T, typename... Args>
  static byte* inject(byte* here, T&& h, Args&&... args) noexcept
  {
    auto alsohere = inject(here, h);
    (reinterpret_cast<BaseHeader*>(here))->flagsNextHeader = true;
    return inject(alsohere, std::forward<Args>(args)...);
  }

  // just to terminate the recursion
  static byte* inject(byte* here) noexcept
  {
    printf("  Stack: injecting NOTHING %p\n", here);
    return here;
  }
};

void hexDump(const char* desc, const void* voidaddr, size_t len, size_t max = 512)
{
  size_t i;
  unsigned char buff[17]; // stores the ASCII data
  memset(&buff[0], '\0', 17);
  const byte* addr = reinterpret_cast<const byte*>(voidaddr);

  // Output description if given.
  if (desc != nullptr)
    printf("%s, ", desc);
  printf("%zu bytes:", len);
  if (max > 0 && len > max) {
    len = max; // limit the output if requested
    printf(" output limited to %zu bytes\n", len);
  }
  else {
    printf("\n");
  }

  // In case of null pointer addr
  if (addr == nullptr) {
    printf("  nullptr, size: %zu\n", len);
    return;
  }

  // Process every byte in the data.
  for (i = 0; i < len; i++) {
    // Multiple of 16 means new line (with line offset).
    if ((i % 16) == 0) {
      // Just don't print ASCII for the zeroth line.
      if (i != 0)
        printf("  %s\n", buff);

      // Output the offset.
      // printf ("  %04x ", i);
      printf("  %p ", &addr[i]);
    }

    // Now the hex code for the specific character.
    printf(" %02x", (unsigned int)addr[i]);

    // And store a printable ASCII character for later.
    if (((const int)addr[i] < 0x20) || ((const int)addr[i] > 0x7e))
      buff[i % 16] = '.';
    else
      buff[i % 16] = (const int)addr[i];
    buff[(i % 16) + 1] = '\0';
    fflush(stdout);
  }

  // Pad out last line if not exactly 16 characters.
  while ((i % 16) != 0) {
    printf("   ");
    fflush(stdout);
    i++;
  }

  // And print the final ASCII bit.
  printf("  %s\n", buff);
  fflush(stdout);
}
