# MicroProtocolFramework

MicroProtocolFramework — пакет на C++23 с тремя намеренно разделёнными
модулями: header-only ядром протокола, компилируемой транспортной частью и
примитивами поддержки безопасности. Он предоставляет версионированные
сообщения, ограниченные кодеки, фрейминг VSP1, инкрементальное декодирование
потоков, RAII-сокеты TCP/UDP, ограниченный reconnect, безопасное стирание
памяти, компактные разрешения и композицию аутентифицированных фреймов.

**Текущая версия:** `0.3.0-beta`

## Назначение

Транспортный код должен передавать байты, не владея схемами сообщений. Код
протокола должен формировать сообщения, не владея сокетами. Пакет сохраняет эти
модули раздельными, хотя они находятся в одном репозитории и выпуске:

```text
vosp::transport            TCP / UDP / reconnect / blocking backpressure
             |
             v
vosp::protocol        сообщения / версии / кодеки / фреймы VSP1
             ^
             |
vosp::security        защищённые байты / разрешения / authentication TLV
```

Требуется только MicroContractsFramework. Криптографические алгоритмы напрямую
предоставляет приложение через MCF-совместимый digest- или authenticator-provider;
пакет не изобретает собственную криптографию. Локальный IPC, сжатие и загрузка
плагинов не входят в runtime версии `0.3.0`.

## Публичный API

- `Version`, `VersionRange`, `negotiate_version` — совместимость версий;
- `BinaryWriter`, `BinaryReader` — проверяемые big-endian примитивы и строки;
- `Utf8Codec`, `BytesCodec` — ограниченные текстовые и байтовые payload;
- `Message`, `MessageView`, `Extension` — владеющие и невладеющие значения;
- `FrameCodec` — точное и префиксное кодирование/декодирование VSP1;
- `StreamDecoder` — обработка фрагментированных и объединённых потоков;
- `Limits` — пределы payload, расширений, фрейма и буферизованных байтов;
- `TcpStream`, `TcpListener` — move-only владение TCP, полная отправка и
  ограниченное повторное подключение;
- `UdpSocket`, `Datagram` — ограниченный ввод-вывод сообщений;
- `IpEndpoint`, `IoOptions`, `ReconnectPolicy` — явная сетевая политика;
- `SecureBuffer`, `secure_erase`, `constant_time_equal` — ограниченное
  move-only владение секретами и платформенное стирание;
- `PermissionSet` — разрешения enum без выделений в одном 64-битном слове;
- `authentication_extension`, `authentication_tag` — прямая композиция с
  зарезервированным authentication TLV VSP1.

Компактный фасад предоставляет `vsp::Protocol`, `vsp::ProtocolMessage`,
`vsp::ProtocolStream`, `vsp::ProtocolVersion` и `vsp::ProtocolLimits`.
Транспортные псевдонимы включают `vsp::TcpStream`, `vsp::TcpListener`,
`vsp::TcpEndpoint` и `vsp::UdpSocket`. Безопасность предоставляет
`vosp::SecureBuffer` и явное пространство имён `vosp::security`.

## Быстрый старт

```cpp
#include <vosp/protocol.hpp>

#include <string>

vosp::protocol::Utf8Codec text;
auto payload = text.encode(std::string{"hello"});
if (!payload) {
    return 1;
}

vsp::Protocol protocol;
auto frame = protocol.encode(vsp::ProtocolMessage{
    vsp::ProtocolVersion{1, 0}, 7, 42, std::move(*payload)});
if (!frame) {
    return 2;
}

auto message = protocol.decode(*frame);
return message && message->correlation_id() == 42 ? 0 : 3;
```

При TCP-фрагментации передавайте каждый полученный диапазон байтов в
`vsp::ProtocolStream::push()` и вызывайте `next()`, пока он не вернёт пустой
`optional`. Некорректные данные обрабатываются fail-closed и остаются в буфере
до `reset()`; транспорт решает, закрыть или повторно синхронизировать соединение.

Транспорт используется напрямую, без адаптера протокола:

```cpp
#include <vosp/protocol.hpp>
#include <vosp/transport.hpp>

vsp::Protocol codec;
vsp::TcpStream connection;
auto connected = connection.connect(vsp::TcpEndpoint{"127.0.0.1", 9000});
auto frame = codec.encode(vsp::ProtocolMessage{
    vsp::ProtocolVersion{1, 0}, 7, 42, {std::byte{0x2a}}});
if (connected && frame) {
    auto sent = connection.send_all(*frame);
}
```

Поддержка безопасности напрямую соединяется с расширениями протокола:

```cpp
#include <vosp/security.hpp>

std::array tag{std::byte{0x10}, std::byte{0x20}}; // результат provider
auto extension = vosp::security::authentication_extension(tag);
auto secret = vosp::SecureBuffer::copy_from(tag);
if (!extension || !secret) {
    return 1;
}
```

Provider реализует структурный концепт `vosp::contracts::DigestProvider` или
`MessageAuthenticator`. Адаптеры и иерархия наследования не требуются.

## Сборка и тестирование

Требования: CMake 3.25, C++23 и MicroContractsFramework 0.9.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPROTOCOL_CONTRACTS_SOURCE_DIR=/path/to/MicroContractsFramework
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Установленный пакет:

```cmake
find_package(mprotocol 0.2 REQUIRED CONFIG)
target_link_libraries(application PRIVATE vosp::protocol vosp::transport)
```

Опциональные цели:

- `-DMPROTOCOL_BUILD_BENCHMARKS=ON`;
- `-DMPROTOCOL_BUILD_FUZZERS=ON` с Clang/libFuzzer;
- `-DMPROTOCOL_ENABLE_SANITIZERS=ON`.

## Измеренная базовая производительность

Встроенный опциональный benchmark измеряет полный владеющий цикл `encode()` +
`decode()`. Поэтому результат включает выделение памяти фрейма, валидацию,
преобразование порядка байтов, копирование payload и выделение декодированного
сообщения.

Локальная базовая линия Windows (MSVC Release, AMD Ryzen 7 PRO 1700X, медиана
пяти запусков):

| Payload | Циклов/с | Пропускная способность payload |
|---:|---:|---:|
| 0 B | 3.81 M | управляющие фреймы |
| 64 B | 2.94 M | 0.188 GB/s |
| 256 B | 2.84 M | 0.728 GB/s |
| 1 KiB | 2.42 M | 2.48 GB/s |
| 4 KiB | 1.89 M | 7.72 GB/s |
| 64 KiB | 164.7 K | 10.79 GB/s |
| 1 MiB | 1.32 K | 1.38 GB/s |

Эти значения — воспроизводимая базовая линия разработки, а не межмашинная
гарантия или некорректное сравнение с генераторами схем. Входные параметры,
число итераций и медианы хранятся в
[результатах benchmark](benchmarks/results/windows-msvc-ryzen-1700x.csv).

Транспортный benchmark измеряет request/echo round trip одного клиента через
loopback. Медиана пяти локальных запусков на том же Windows/MSVC/Ryzen 7 PRO
1700X:

| Транспорт | Payload | Round trip/с | Двунаправленная пропускная способность |
|---|---:|---:|---:|
| TCP | 64 B | 13.44 K | 1.72 MB/s |
| UDP | 64 B | 20.36 K | 2.61 MB/s |
| TCP | 1 KiB | 12.29 K | 25.17 MB/s |
| UDP | 1 KiB | 20.08 K | 41.13 MB/s |
| TCP | 65,507 B | 5.75 K | 753.14 MB/s |
| UDP | 65,507 B | 4.61 K | 603.81 MB/s |

Это чувствительная к задержке loopback-база, а не пропускная способность
интернета и не сравнение со сторонними библиотеками. Медианы хранятся в
[`benchmarks/results/windows-msvc-ryzen-1700x-transport.csv`](benchmarks/results/windows-msvc-ryzen-1700x-transport.csv).
Security benchmark измеряет сравнение диапазонов равной длины, обновление
разрешений и платформенное стирание. Медиана пяти запусков на том же хосте:

| Payload | Сравнение | Обновление разрешения | Безопасное стирание |
|---:|---:|---:|---:|
| 64 B | 7.99 ns | 1.17 ns | 33.18 ns |
| 4 KiB | 135.71 ns | 1.15 ns | 147.25 ns |

C++ не гарантирует абсолютное постоянное время: `constant_time_equal` имеет
независимый от содержимого поток управления для равных публичных длин. Если
криптографический provider предоставляет проверенный verify-примитив, следует
использовать его. Сырые данные находятся в
[`benchmarks/results/windows-msvc-ryzen-1700x-security.csv`](benchmarks/results/windows-msvc-ryzen-1700x-security.csv).
Benchmarks являются исходными целями разработки и не устанавливаются с пакетом.

## Безопасность и жизненный цикл

- Формат кодируется поле за полем; padding C++ и порядок байтов хоста никогда
  не сериализуются.
- Размеры payload, TLV, фрейма и потокового буфера ограничиваются до изменения.
- `Message` владеет байтами; `MessageView` не должен переживать исходное хранилище.
- `FrameCodec`, кодеки значений и неизменяемые сообщения можно использовать
  параллельно как отдельные объекты.
- `StreamDecoder`, `BinaryReader` и `BinaryWriter` — изменяемые single-owner
  объекты; совместное использование требует внешней синхронизации.
- Классы сокетов — move-only RAII-владельцы; `close()` идемпотентен и также
  вызывается при уничтожении.
- Повторные подключения ограничены политикой и жёстким пределом 1024 попытки.
- Отправка и приём UDP отклоняют пределы payload свыше 65 507 байт.
- Один объект сокета является single-owner и требует внешней синхронизации при
  совместном использовании между потоками.
- Неизвестные флаги и непрозрачные идентификаторы расширений сохраняются как
  данные протокола; семантическая валидация принадлежит владельцу расширения.
- `SecureBuffer` является move-only, ограничен 64 MiB и стирает логические
  байты при clear, move-присваивании и уничтожении системным примитивом.
- Безопасное стирание не блокирует страницы и не удаляет независимые копии;
  для более сильной модели угроз нужен проверенный provider или средство ОС.
- Enum разрешений должен быть непрерывным в `[0, Count)`, где `Count <= 64`.
- Helpers authentication TLV проверяют тип и предел до 4096 байт; возвращаемый
  span заимствует хранилище расширения.

Смотрите [архитектуру](docs/ARCHITECTURE.md),
[стабильные контракты](docs/CONTRACTS.md) и
[спецификацию wire format](docs/WIRE_FORMAT.md).

Лицензия MIT.
