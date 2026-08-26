#include <vosp/protocol.hpp>

struct InvalidModel {};

int main() {
    vosp::protocol::FrameCodec<InvalidModel> codec;
    (void)codec;
}
