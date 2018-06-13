#include "test.h"
//__________________________________________________________________________________________________
int main()
{
  FairMQTransportFactory factory;
  ChannelResource channelResource(&factory);
  using o2vector = std::vector<elem, SpectatorAllocator<byte>>;

  printf("\nmake vector with SpectatorAllocator\n");
  o2vector vector(SpectatorAllocator<elem>{ &channelResource });
  vector.reserve(3);
  vector.emplace_back(1);
  vector.emplace_back(2);
  vector.emplace_back(3);
  vector.emplace_back(4);
  printf("vector: %i %i %i %i\n", vector[0].content, vector[1].content, vector[2].content, vector[3].content);
  auto mes = getMessage(std::move(vector));
  print(mes.get());
  printf("vector size: %li\n", vector.size());

  printf("\nadopt\n");
  auto message = std::make_unique<FairMQMessage>(3 * sizeof(elem));
  SpectatorMessageResource messageResource{ message.get() };
  printf("address of resource3: %p\n", &messageResource);
  elem tmpBuf[3] = { 3, 2, 1 };
  std::memcpy(message->GetData(), tmpBuf, 3 * sizeof(elem));

  {
    printf("\nadoptVector() by value with a SpectatorMessageResource\n");
    auto v = adoptVector<elem>(3, &messageResource);
    printf("v: %i %i %i\n", v[0].content, v[1].content, v[2].content);
  }

  {
    printf("\nadoptVector() by value with an OwningMessageSpectatorAllocator\n");
    auto vv = adoptVector<elem>(3, &channelResource, std::move(message));
    printf("vv: %i %i %i\n", vv[0].content, vv[1].content, vv[2].content);
    auto vvv = std::move(vv);
    printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);
    auto mes = getMessage(std::move(vv));
    print(mes.get());
    auto mess = getMessage(std::move(vvv));
    print(mess.get());
  }

  printf("\nreturn\n");
  return 0;
}
