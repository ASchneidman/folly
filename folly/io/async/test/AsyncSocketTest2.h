/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <deque>
#include <exception>
#include <functional>
#include <string>

#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/synchronization/RWSpinLock.h>

namespace folly {
namespace test {

/**
 * Helper ConnectionEventCallback class for the test code.
 * It maintains counters protected by a spin lock.
 */
class TestConnectionEventCallback
    : public AsyncServerSocket::ConnectionEventCallback {
 public:
  void onConnectionAccepted(
      const NetworkSocket /* socket */,
      const SocketAddress& /* addr */) noexcept override {
    std::unique_lock holder(spinLock_);
    connectionAccepted_++;
  }

  void onConnectionAcceptError(const int /* err */) noexcept override {
    std::unique_lock holder(spinLock_);
    connectionAcceptedError_++;
  }

  void onConnectionDropped(
      const NetworkSocket /* socket */,
      const SocketAddress& /* addr */) noexcept override {
    std::unique_lock holder(spinLock_);
    connectionDropped_++;
  }

  void onConnectionEnqueuedForAcceptorCallback(
      const NetworkSocket /* socket */,
      const SocketAddress& /* addr */) noexcept override {
    std::unique_lock holder(spinLock_);
    connectionEnqueuedForAcceptCallback_++;
  }

  void onConnectionDequeuedByAcceptorCallback(
      const NetworkSocket /* socket */,
      const SocketAddress& /* addr */) noexcept override {
    std::unique_lock holder(spinLock_);
    connectionDequeuedByAcceptCallback_++;
  }

  void onBackoffStarted() noexcept override {
    std::unique_lock holder(spinLock_);
    backoffStarted_++;
  }

  void onBackoffEnded() noexcept override {
    std::unique_lock holder(spinLock_);
    backoffEnded_++;
  }

  void onBackoffError() noexcept override {
    std::unique_lock holder(spinLock_);
    backoffError_++;
  }

  unsigned int getConnectionAccepted() const {
    std::shared_lock holder(spinLock_);
    return connectionAccepted_;
  }

  unsigned int getConnectionAcceptedError() const {
    std::shared_lock holder(spinLock_);
    return connectionAcceptedError_;
  }

  unsigned int getConnectionDropped() const {
    std::shared_lock holder(spinLock_);
    return connectionDropped_;
  }

  unsigned int getConnectionEnqueuedForAcceptCallback() const {
    std::shared_lock holder(spinLock_);
    return connectionEnqueuedForAcceptCallback_;
  }

  unsigned int getConnectionDequeuedByAcceptCallback() const {
    std::shared_lock holder(spinLock_);
    return connectionDequeuedByAcceptCallback_;
  }

  unsigned int getBackoffStarted() const {
    std::shared_lock holder(spinLock_);
    return backoffStarted_;
  }

  unsigned int getBackoffEnded() const {
    std::shared_lock holder(spinLock_);
    return backoffEnded_;
  }

  unsigned int getBackoffError() const {
    std::shared_lock holder(spinLock_);
    return backoffError_;
  }

 private:
  mutable folly::RWSpinLock spinLock_;
  unsigned int connectionAccepted_{0};
  unsigned int connectionAcceptedError_{0};
  unsigned int connectionDropped_{0};
  unsigned int connectionEnqueuedForAcceptCallback_{0};
  unsigned int connectionDequeuedByAcceptCallback_{0};
  unsigned int backoffStarted_{0};
  unsigned int backoffEnded_{0};
  unsigned int backoffError_{0};
};

/**
 * Helper AcceptCallback class for the test code
 * It records the callbacks that were invoked, and also supports calling
 * generic std::function objects in each callback.
 */
class TestAcceptCallback : public AsyncServerSocket::AcceptCallback {
 public:
  enum EventType { TYPE_START, TYPE_ACCEPT, TYPE_ERROR, TYPE_STOP };
  struct EventInfo {
    EventInfo(folly::NetworkSocket fd_, const folly::SocketAddress& addr)
        : type(TYPE_ACCEPT), fd(fd_), address(addr), errorMsg() {}
    explicit EventInfo(const std::string& msg)
        : type(TYPE_ERROR), fd(), address(), errorMsg(msg) {}
    explicit EventInfo(EventType et) : type(et), fd(), address(), errorMsg() {}

    EventType type;
    folly::NetworkSocket fd; // valid for TYPE_ACCEPT
    folly::SocketAddress address; // valid for TYPE_ACCEPT
    std::string errorMsg; // valid for TYPE_ERROR
  };
  typedef std::deque<EventInfo> EventList;

  TestAcceptCallback()
      : connectionAcceptedFn_(),
        acceptErrorFn_(),
        acceptStoppedFn_(),
        events_() {}

  std::deque<EventInfo>* getEvents() { return &events_; }

  void setConnectionAcceptedFn(
      const std::function<void(NetworkSocket, const folly::SocketAddress&)>&
          fn) {
    connectionAcceptedFn_ = fn;
  }
  void setAcceptErrorFn(const std::function<void(const std::exception&)>& fn) {
    acceptErrorFn_ = fn;
  }
  void setAcceptStartedFn(const std::function<void()>& fn) {
    acceptStartedFn_ = fn;
  }
  void setAcceptStoppedFn(const std::function<void()>& fn) {
    acceptStoppedFn_ = fn;
  }

  void connectionAccepted(
      NetworkSocket fd,
      const folly::SocketAddress& clientAddr,
      AcceptInfo /* info */) noexcept override {
    events_.emplace_back(fd, clientAddr);

    if (connectionAcceptedFn_) {
      connectionAcceptedFn_(fd, clientAddr);
    }
  }
  void acceptError(folly::exception_wrapper ex) noexcept override {
    events_.emplace_back(ex.what().toStdString());

    if (acceptErrorFn_) {
      acceptErrorFn_(*ex.get_exception());
    }
  }
  void acceptStarted() noexcept override {
    events_.emplace_back(TYPE_START);

    if (acceptStartedFn_) {
      acceptStartedFn_();
    }
  }
  void acceptStopped() noexcept override {
    events_.emplace_back(TYPE_STOP);

    if (acceptStoppedFn_) {
      acceptStoppedFn_();
    }
  }

 private:
  std::function<void(NetworkSocket, const folly::SocketAddress&)>
      connectionAcceptedFn_;
  std::function<void(const std::exception&)> acceptErrorFn_;
  std::function<void()> acceptStartedFn_;
  std::function<void()> acceptStoppedFn_;

  std::deque<EventInfo> events_;
};

class TestConnectCallback : public AsyncSocket::ConnectCallback {
 public:
  void preConnect(NetworkSocket fd) override {
    int one = 1;
    netops::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  }
  void connectSuccess() noexcept override {}
  void connectErr(const AsyncSocketException& /*ex*/) noexcept override {}
};

} // namespace test
} // namespace folly
