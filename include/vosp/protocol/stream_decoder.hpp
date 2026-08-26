#pragma once

/** @file stream_decoder.hpp Incremental fail-closed frame decoder. */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <vosp/protocol/frame.hpp>

namespace vosp::protocol {
template <vosp::contracts::ErrorModel ErrorModel = Model>
/** @brief Single-owner decoder for fragmented and coalesced byte streams. */
class StreamDecoder final {
  public:
    explicit StreamDecoder(Limits limits = {}) : limits_{limits}, codec_{limits} {
        static_assert(detail::ResultFor<ErrorModel, std::optional<Message>>);
    }

    [[nodiscard]] typename ErrorModel::OperationResult push(std::span<const std::byte> bytes) {
        if (!limits_.valid()) {
            return detail::failure<ErrorModel>(ErrorCode::invalid_argument,
                                               "protocol limits are inconsistent");
        }
        compact();
        if (cursor_ != 0 &&
            (buffer_.size() > limits_.max_buffered_bytes ||
             bytes.size() > limits_.max_buffered_bytes -
                                std::min(limits_.max_buffered_bytes, buffer_.size()))) {
            compact_consumed();
        }
        if (buffer_.size() > limits_.max_buffered_bytes ||
            bytes.size() > limits_.max_buffered_bytes - buffer_.size()) {
            return detail::failure<ErrorModel>(ErrorCode::limit_exceeded,
                                               "protocol stream buffer limit exceeded");
        }
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
        return {};
    }

    [[nodiscard]] typename ErrorModel::template Result<std::optional<Message>> next() {
        if (buffered_size() == 0) {
            return std::optional<Message>{};
        }
        const std::span<const std::byte> available{buffer_.data() + cursor_,
                                                   buffer_.size() - cursor_};
        auto inspected = codec_.probe(available);
        if (!inspected) {
            return typename ErrorModel::template Result<std::optional<Message>>{
                std::unexpected{inspected.error()}};
        }
        if (inspected->state == FrameState::incomplete) {
            return std::optional<Message>{};
        }
        auto decoded = codec_.decode_prefix(available);
        if (!decoded) {
            return typename ErrorModel::template Result<std::optional<Message>>{
                std::unexpected{decoded.error()}};
        }
        cursor_ += decoded->consumed;
        std::optional<Message> message{std::move(decoded->message)};
        compact();
        return message;
    }

    [[nodiscard]] std::size_t buffered_size() const noexcept {
        return buffer_.size() - cursor_;
    }

    void reset() noexcept {
        buffer_.clear();
        cursor_ = 0;
    }

  private:
    void compact() {
        if (cursor_ == 0) {
            return;
        }
        if (cursor_ == buffer_.size()) {
            reset();
            return;
        }
        if (cursor_ >= 4096U && cursor_ >= buffer_.size() / 2U) {
            compact_consumed();
        }
    }

    void compact_consumed() {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(cursor_));
        cursor_ = 0;
    }

    Limits limits_;
    FrameCodec<ErrorModel> codec_;
    std::vector<std::byte> buffer_;
    std::size_t cursor_ = 0;
};
} // namespace vosp::protocol
