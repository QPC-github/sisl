//
// Created by Kadayam, Hari on 12/21/18.
//
#ifndef SISL_FDS_WAITFREE_WRITE_DS_HPP
#define SISL_FDS_WAITFREE_WRITE_DS_HPP

#include "utility/thread_buffer.hpp"
#include "utility/urcu_helper.hpp"
#include <tuple>

namespace sisl { namespace fds {

template <typename DS, typename... Args>
class WrapperBuf {
public:
    template <class... Args1>
    WrapperBuf(Args1&&... args) :
            m_safe_buf(std::forward<Args1>(args)...),
            m_args(std::forward<Args1>(args)...) {}

    sisl::urcu_ptr< DS > get_safe() { return m_safe_buf.get(); }
    std::shared_ptr< DS > rotate() {
        //auto n = m_safe_buf.make_and_exchange(std::forward<Args>(m_args)...);
        //auto n = std::apply(m_safe_buf.make_and_exchange, m_args);
        return _make_and_exchange(m_args, std::index_sequence_for<Args...>());
    }

    std::unique_ptr< DS > make_new() {
        //return std::make_unique< DS >(std::forward<Args>(m_args)...);
        //return std::apply(std::make_unique< DS >, m_args);
        return _make_new(m_args, std::index_sequence_for<Args...>());
    }

private:
    template<std::size_t... Is>
    std::unique_ptr< DS > _make_new(const std::tuple<Args...>& tuple, std::index_sequence<Is...>) {
        return std::make_unique<DS>(std::get<Is>(tuple)...);
    }

    template<std::size_t... Is>
    std::shared_ptr< DS > _make_and_exchange(const std::tuple<Args...>& tuple, std::index_sequence<Is...>) {
        return m_safe_buf.make_and_exchange(std::get<Is>(tuple)...);
    }

private:
    sisl::urcu_data< DS, Args... > m_safe_buf;
    std::tuple< Args... >         m_args;
};

/* This class implements a generic wait free writer framework to build a wait free writer structures on top of it.
 * The reader side is syncronized using locks and expected to perform very slow. However, writer side are wait free
 * using rcu and per thread buffer. Thus it can be typically used on structures which are very frequently updated but
 * rarely read (say metrics collection, list of garbage entries to cleanup etc)..
 */
template <typename DS, typename... Args>
class wisr_framework {
public:
    template <class... Args1>
    wisr_framework(Args1&&... args) :
            m_buffer(std::forward<Args1>(args)...) {
        m_base_obj = m_buffer->make_new();
    }

    DS* insertable() {
        return m_buffer->get_safe().get();
    }

    DS* accessible() {
        std::lock_guard<std::mutex> lg(m_rotate_mutex);
        _rotate();
        return m_base_obj.get();
    }

    std::unique_ptr< DS > get_copy_and_reset() {
        std::lock_guard<std::mutex> lg(m_rotate_mutex);
        _rotate();

        auto ret = std::move(m_base_obj);
        m_base_obj = m_buffer->make_new();
        return std::move(ret);
    }

private:
    // This method assumes that rotate mutex is already held
    void _rotate() {
        auto base_raw = m_base_obj.get();
        m_buffer.access_all_threads([base_raw](WrapperBuf<DS, Args...> *ptr) {
            auto old_ptr = ptr->rotate();
            DS::merge(base_raw, old_ptr.get());
            return true;
        });
    }
private:
    sisl::ThreadBuffer< WrapperBuf< DS, Args... >, Args... > m_buffer;
    std::mutex m_rotate_mutex;
    std::unique_ptr< DS > m_base_obj;
};
}} // namespace sisl::fds


#endif //SISL_FDS_WAITFREE_WRITE_DS_HPP
