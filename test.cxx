#include "test.h"
//__________________________________________________________________________________________________
int main()
{
  FairMQTransportFactory factory;
  ChannelResource channelResource(&factory);
  using o2vector = std::vector<elem, FastSpectatorAllocator>;

  printf("\nmake vector with FastSpectatorAllocator\n");
  o2vector vector(3, FastSpectatorAllocator{ &channelResource });
  // vector.reserve(3);
  vector.emplace_back(1);
  vector.emplace_back(2);
  vector.emplace_back(3);
  vector.emplace_back(4);
  printf("vector: %i %i %i %i\n", vector[0].content, vector[1].content, vector[2].content, vector[3].content);
  auto mes = getMessage(vector);
  print(mes);
  printf("vector size: %li\n", vector.size());

  printf("\nmake vector with pmr\n");
  std::vector<elem, boost::container::pmr::polymorphic_allocator<byte>> vectorPol(
    3, boost::container::pmr::polymorphic_allocator<byte>{ &channelResource });
  // vectorPol.reserve(3);
  vectorPol.emplace_back(1);
  vectorPol.emplace_back(2);
  vectorPol.emplace_back(3);
  vectorPol.emplace_back(4);
  printf("vectorPol: %i %i %i %i\n", vectorPol[0].content, vectorPol[1].content, vectorPol[2].content,
         vectorPol[3].content);
  auto mesPol = getMessage(vectorPol);
  print(mesPol);
  printf("vectorPol size: %li\n", vectorPol.size());

  printf("\nswap test:\n");
  o2vector v(3, FastSpectatorAllocator{ &channelResource });
  v.swap(vector);
  printf("v: %i %i %i\n", v[0].content, v[1].content, v[2].content);
  printf("vector: %i %i %i\n", vector[0].content, vector[1].content, vector[2].content);

  printf("\nadopt\n");
  auto message = std::make_unique<FairMQMessage>(3 * sizeof(elem));
  SpectatorMessageResource messageResource(message.get());
  printf("address of resource3: %p\n", &messageResource);
  elem tmpBuf[3] = { 3, 2, 1 };
  std::memcpy(message->GetData(), tmpBuf, 3 * sizeof(elem));

  o2vector vv = o2vector(3, FastSpectatorAllocator{ &messageResource });
  printf("vv: %i %i %i\n", vv[0].content, vv[1].content, vv[2].content);

  o2vector* newvv = new o2vector(3, FastSpectatorAllocator{ &messageResource });
  printf("newvv: %i %i %i\n", (*newvv)[0].content, (*newvv)[1].content, (*newvv)[2].content);

  printf("\nget message from vv\n");
  boost::container::pmr::polymorphic_allocator<byte> alv = vv.get_allocator();
  printf("resource from allocator from vv: %p\n", alv.resource());

  printf("\nadoptVector()\n");
  auto vvv = adoptVector<elem>(3, messageResource);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  printf("\ncopy vvv\n");
  o2vector cvvv = copyVector(vvv);
  printf("cvvv: %i %i %i\n", cvvv[0].content, cvvv[1].content, cvvv[2].content);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  Adopt<elem>::OutputType outvec;
  {
    printf("\nadopt vector with temp resource\n");
    printf("1---\n");
    auto vvvv = Adopt<elem>::Vector(3, message.get());
    printf("vvvv@%p, %i %i %i\n", vvvv.get(), (*vvvv)[0].content, (*vvvv)[1].content, (*vvvv)[2].content);
    printf("2---\n");
    outvec=std::move(vvvv);
    auto vvvvv = Adopt<elem>::Vector(3, message.get());
    printf("3---\n");
    vvvvv.release();
    printf("4---\n");
  }
  printf("5---\n");
  printf("outvec@%p, %i %i %i\n", outvec.get(), (*outvec)[0].content, (*outvec)[1].content, (*outvec)[2].content);
  printf("outvec size: %li\n", outvec->size());

  decltype(adoptVector<elem>(3,message.get())) outvec2;
  {
    printf("\nadopt vector with temp resource 2\n");
    printf("1---\n");
    auto vvvv = adoptVector<elem>(3, message.get());
    printf("vvvv@%p, %i %i %i\n", vvvv.get(), (*vvvv)[0].content, (*vvvv)[1].content, (*vvvv)[2].content);
    printf("2---\n");
    outvec2=std::move(vvvv);
    auto vvvvv = adoptVector<elem>(3, message.get());
    printf("3---\n");
    vvvvv.release();
    printf("4---\n");
  }
  printf("5---\n");
  printf("outvec2@%p, %i %i %i\n", outvec2.get(), (*outvec2)[0].content, (*outvec2)[1].content, (*outvec2)[2].content);
  printf("outvec2 size: %li\n", outvec2->size());

  printf("cvvv: %i %i %i\n", cvvv[0].content, cvvv[1].content, cvvv[2].content);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);

  printf("\nstack ctor\n");
  auto ui = std::make_unique<int>(3);
  Stack stack{ BaseHeader{}, BaseHeader{} };

  printf("\nreturn\n");
  return 0;
}
