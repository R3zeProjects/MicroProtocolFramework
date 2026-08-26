# MicroProtocolFramework

MicroProtocolFramework — это ядро протокола на C++23 для версионированных
сообщений, ограниченных двоичных и текстовых кодеков, стабильного фрейминга и
инкрементального декодирования потоков. Это слой формата передачи экосистемы
VOSP. Он не реализует сокеты, повторное подключение, криптографию, сжатие или
загрузку плагинов.

**Текущая версия:** `0.1.0-beta`

## Назначение

Транспортный код должен передавать байты, не владея схемами сообщений. Код
безопасности должен аутентифицировать байты, не владея фреймингом. Плагины
должны согласовывать версии без привязки ABI к сетевому бэкенду. Этот framework
явно сохраняет такие границы:

```text
MicroTransportFramework   сокеты / IPC / reconnect / backpressure
             |
             v
MicroProtocolFramework    сообщения / версии / кодеки / фреймы VSP1
             ^
             |
MicroSecurityFramework    расширения checksum / authentication / encryption
MicroPluginFramework      кодеки манифестов и управляющих сообщений
```

Требуется только MicroContractsFramework. Конкретную модель ошибок можно
заменить через концепты MCF; готовая модель использует `std::expected`.

## Публичный API

- `Version`, `VersionRange`, `negotiate_version` — совместимость версий;
- `BinaryWriter`, `BinaryReader` — проверяемые big-endian примитивы и строки;
- `Utf8Codec`, `BytesCodec` — ограниченные текстовые и байтовые payload;
- `Message`, `MessageView`, `Extension` — владеющие и невладеющие значения;
- `FrameCodec` — точное и префиксное кодирование/декодирование VSP1;
- `StreamDecoder` — обработка фрагментированных и объединённых потоков;
- `Limits` — пределы payload, расширений, фрейма и буферизованных байтов.

Компактный фасад предоставляет `vsp::Protocol`, `vsp::ProtocolMessage`,
`vsp::ProtocolStream`, `vsp::ProtocolVersion` и `vsp::ProtocolLimits`.

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

При фрагментации в стиле TCP передавайте каждый полученный диапазон байтов в
`vsp::ProtocolStream::push()` и вызывайте `next()`, пока функция не вернёт
пустой optional. Некорректные данные обрабатываются по принципу fail-closed и
остаются в буфере до `reset()`; транспорт решает, закрыть соединение или
выполнить повторную синхронизацию.

## Сборка и тестирование

Требования: CMake 3.25, C++23 и MicroContractsFramework 0.7.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMPROTOCOL_CONTRACTS_SOURCE_DIR=/path/to/MicroContractsFramework
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Установленный пакет:

```cmake
find_package(mprotocol 0.1 REQUIRED CONFIG)
target_link_libraries(application PRIVATE vosp::protocol)
```

Опциональные цели:

- `-DMPROTOCOL_BUILD_BENCHMARKS=ON`;
- `-DMPROTOCOL_BUILD_FUZZERS=ON` с Clang/libFuzzer;
- `-DMPROTOCOL_ENABLE_SANITIZERS=ON`.

## Измеренный базовый уровень

Встроенный опциональный benchmark измеряет полный владеющий цикл `encode()` +
`decode()`. Поэтому результат включает выделение памяти для фрейма, валидацию,
преобразование порядка байтов, копирование payload и выделение памяти для
декодированного сообщения.

Локальный базовый результат Windows (MSVC Release, AMD Ryzen 7 PRO 1700X,
медиана пяти запусков):

| Payload | Циклов/с | Пропускная способность payload |
|---:|---:|---:|
| 0 B | 3.81 M | управляющие фреймы |
| 64 B | 2.94 M | 0.188 GB/s |
| 256 B | 2.84 M | 0.728 GB/s |
| 1 KiB | 2.42 M | 2.48 GB/s |
| 4 KiB | 1.89 M | 7.72 GB/s |
| 64 KiB | 164.7 K | 10.79 GB/s |
| 1 MiB | 1.32 K | 1.38 GB/s |

Эти значения — воспроизводимый базовый уровень разработки, а не гарантия для
другого компьютера и не некорректное сравнение с компиляторами схем. Исходные
параметры, количество итераций и медианы сохранены в
[результате benchmark](benchmarks/results/windows-msvc-ryzen-1700x.csv).
Benchmark является только целью для разработки из исходников и не
устанавливается вместе с пакетом.

## Безопасность и жизненный цикл

- Представление wire format кодируется по полям; padding C++ и порядок байтов
  хоста никогда не сериализуются.
- Размеры payload, TLV, фрейма и потокового буфера проверяются до изменения.
- `Message` владеет байтами; `MessageView` не должен переживать хранилище, на
  которое он ссылается.
- `FrameCodec`, кодеки значений и неизменяемые сообщения можно параллельно
  использовать как отдельные объекты.
- `StreamDecoder`, `BinaryReader` и `BinaryWriter` — изменяемые объекты с одним
  владельцем; при совместном доступе требуется внешняя синхронизация.
- Неизвестные биты флагов и непрозрачные ID расширений сохраняются как данные
  протокола; семантическая проверка принадлежит framework, который владеет
  расширением.

См. [архитектуру](docs/ARCHITECTURE.md),
[стабильные контракты](docs/CONTRACTS.md) и
[спецификацию wire format](docs/WIRE_FORMAT.md).

Распространяется по лицензии MIT.
