#include "test.h"
//__________________________________________________________________________________________________
int main()
{
  FairMQTransportFactory factory;
  ChannelResource channelResource(&factory);
  using o2vector = std::vector<elem, SpectatorAllocator<byte>>;

  printf("\nmake vector with ByteSpectatorAllocator\n");
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
  SpectatorMessageResource messageResource{message.get()};
  printf("address of resource3: %p\n", &messageResource);
  elem tmpBuf[3] = { 3, 2, 1 };
  std::memcpy(message->GetData(), tmpBuf, 3 * sizeof(elem));

  {
  printf("\nadoptVector() by value with a SpectatorMessageResource\n");
  auto vvv = adoptVector<elem>(3, messageResource);
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);
  }

  {
  printf("\nadoptVector() by value with an OwningMessageSpectatorAllocator\n");
  auto vvv = adoptVector<elem>(3, std::move(message));
  printf("vvv: %i %i %i\n", vvv[0].content, vvv[1].content, vvv[2].content);
  }

  printf("\nreturn\n");
  return 0;
}
