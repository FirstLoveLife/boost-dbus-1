// Copyright (c) Benjamin Kietzman (github.com/bkietz)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef DBUS_MESSAGE_HPP
#define DBUS_MESSAGE_HPP

#include <dbus/dbus.h>
#include <dbus/element.hpp>
#include <dbus/endpoint.hpp>
#include <dbus/impl/message_iterator.hpp>
#include <iostream>
#include <vector>
#include <boost/intrusive_ptr.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/utility/enable_if.hpp>

inline void intrusive_ptr_add_ref(DBusMessage* m) { dbus_message_ref(m); }

inline void intrusive_ptr_release(DBusMessage* m) { dbus_message_unref(m); }

namespace dbus {

class message {
 private:
  boost::intrusive_ptr<DBusMessage> message_;

 public:
  /// Create a method call message
  static message new_call(const endpoint& destination,
                          const string& method_name) {
    auto x = message(dbus_message_new_method_call(
        destination.get_process_name().c_str(), destination.get_path().c_str(),
        destination.get_interface().c_str(), method_name.c_str()));
    dbus_message_unref(x.message_.get());
    return x;
  }

  /// Create a method return message
  static message new_return(message& call) {
    auto x = message(dbus_message_new_method_return(call));
    dbus_message_unref(x.message_.get());
    return x;
  }

  /// Create an error message
  static message new_error(message& call, const string& error_name,
                           const string& error_message) {
    auto x = message(dbus_message_new_error(call, error_name.c_str(),
                                            error_message.c_str()));
    dbus_message_unref(x.message_.get());
    return x;
  }

  /// Create a signal message
  static message new_signal(const endpoint& origin, const string& signal_name) {
    auto x = message(dbus_message_new_signal(origin.get_path().c_str(),
                                             origin.get_interface().c_str(),
                                             signal_name.c_str()));
    dbus_message_unref(x.message_.get());
    return x;
  }

  message() = delete;

  message(DBusMessage* m) : message_(m) {}

  operator DBusMessage*() { return message_.get(); }

  operator const DBusMessage*() const { return message_.get(); }

  string get_path() const {
    return sanitize(dbus_message_get_path(message_.get()));
  }

  string get_interface() const {
    return sanitize(dbus_message_get_interface(message_.get()));
  }

  string get_member() const {
    return sanitize(dbus_message_get_member(message_.get()));
  }

  string get_type() const {
    return sanitize(
        dbus_message_type_to_string(dbus_message_get_type(message_.get())));
  }

  string get_signature() const {
    return sanitize(dbus_message_get_signature(message_.get()));
  }

  string get_sender() const {
    return sanitize(dbus_message_get_sender(message_.get()));
  }

  string get_destination() const {
    return sanitize(dbus_message_get_destination(message_.get()));
  }

  uint32 get_serial() { return dbus_message_get_serial(message_.get()); }

  message& set_serial(uint32 serial) {
    dbus_message_set_serial(message_.get(), serial);
    return *this;
  }

  uint32 get_reply_serial() {
    return dbus_message_get_reply_serial(message_.get());
  }

  message& set_reply_serial(uint32 reply_serial) {
    dbus_message_set_reply_serial(message_.get(), reply_serial);
    return *this;
  }

  struct packer {
    impl::message_iterator iter_;
    packer(message& m) { impl::message_iterator::init_append(m, iter_); }
    packer(){};
    template <typename Element>
    packer& pack(const Element& e) {
      return *this << e;
    }

    template <typename Element, typename... Args>
    packer pack(const Element& e, const Args&... args) {
      return this->pack(e).pack(args...);
    }
  };

  template <typename... Args>
  packer pack(const Args&... args) {
    return packer(*this).pack(args...);
  }

  struct unpacker {
    impl::message_iterator iter_;
    unpacker(message& m) { impl::message_iterator::init(m, iter_); }
    unpacker() {}

    template <typename Element>
    unpacker& unpack(Element& e) {
      return *this >> e;
    }

    template <typename Element, typename... Args>
    unpacker& unpack(Element& e, Args&... args) {
      return unpack(e).unpack(args...);
    }
  };

  template <typename... Args>
  unpacker& unpack(Args&... args) {
    return unpacker(*this).unpack(args...);
  }

 private:
  static std::string sanitize(const char* str) {
    return (str == NULL) ? "(null)" : str;
  }
};

template <typename Element>
message::packer operator<<(message m, const Element& e) {
  return message::packer(m).pack(e);
}

template <typename Element>
typename boost::enable_if<is_fixed_type<Element>, message::packer&>::type
operator<<(message::packer& p, const Element& e) {
  p.iter_.append_basic(element<Element>::code, &e);
  return p;
}

// Specialization used to represent "dict" in dbus.
// TODO(ed) generalize for all "map like" types instead of using vector
template <typename Key, typename Value>
message::packer& operator<<(message::packer& p,
                            const std::vector<std::pair<Key, Value>>& v) {
  message::packer sub;
  static const constexpr auto sig =
      element_signature<std::vector<std::pair<Key, Value>>>::code;
  static_assert(std::tuple_size<decltype(sig)>::value > 2,
                "Signature size must be greater than 2 characters long");
  // Skip over the array part "a" of the signature to get the element signature.
  // Open container expects JUST the portion after the "a"
  p.iter_.open_container(sig[0], &sig[1], sub.iter_);
  for (auto& element : v) {
    sub << element;
  }

  p.iter_.close_container(sub.iter_);
  return p;
}

template <typename Element>
message::packer& operator<<(message::packer& p, const std::vector<Element>& v) {
  message::packer sub;
  static const constexpr auto signature =
      element_signature<std::vector<Element>>::code;
  static_assert(std::tuple_size<decltype(signature)>::value > 2,
                "Signature size must be greater than 2 characters long");
  p.iter_.open_container(signature[0], &signature[1], sub.iter_);
  for (auto& element : v) {
    sub << element;
  }

  p.iter_.close_container(sub.iter_);
  return p;
}

inline message::packer& operator<<(message::packer& p, const char* c) {
  p.iter_.append_basic(element<string>::code, &c);
  return p;
}

template <typename Key, typename Value>
inline message::packer& operator<<(message::packer& p,
                                   const std::pair<Key, Value> element) {
  message::packer dict_entry;
  p.iter_.open_container(DBUS_TYPE_DICT_ENTRY, NULL, dict_entry.iter_);
  dict_entry << element.first;
  dict_entry << element.second;
  p.iter_.close_container(dict_entry.iter_);
  return p;
}

inline message::packer& operator<<(message::packer& p, const string& e) {
  const char* c = e.c_str();
  return p << c;
}

inline message::packer& operator<<(message::packer& p, const dbus_variant& v) {
  // Get the dbus typecode  of the variant being packed
  const char* type = boost::apply_visitor(
      [&](auto val) {
        static const constexpr auto sig =
            element_signature<decltype(val)>::code;
        static_assert(std::tuple_size<decltype(sig)>::value == 2,
                      "Element signature for dbus_variant too long.  Expected "
                      "length of 1");
        return &sig[0];
      },
      v);

  message::packer sub;
  p.iter_.open_container(element<dbus_variant>::code, type, sub.iter_);
  boost::apply_visitor([&](auto val) { sub << val; }, v);
  p.iter_.close_container(sub.iter_);

  return p;
}

template <typename Element>
message::unpacker operator>>(message m, Element& e) {
  return message::unpacker(m).unpack(e);
}

template <typename Element>
typename boost::enable_if<is_fixed_type<Element>, message::unpacker&>::type
operator>>(message::unpacker& u, Element& e) {
  u.iter_.get_basic(&e);
  u.iter_.next();
  return u;
}

inline message::unpacker& operator>>(message::unpacker& u, string& s) {
  const char* c;
  u.iter_.get_basic(&c);
  s.assign(c);
  u.iter_.next();
  return u;
}

inline message::unpacker& operator>>(message::unpacker& u, dbus_variant& v) {
  message::unpacker sub;
  u.iter_.recurse(sub.iter_);

  char arg_type = sub.iter_.get_arg_type();

  boost::mpl::for_each<dbus_variant::types>([&](auto t) {
    if (arg_type == element<decltype(t)>::code) {
      decltype(t) val_to_fill;
      sub >> val_to_fill;
      v = val_to_fill;
    }
  });

  u.iter_.next();
  return u;
}

template <typename Key, typename Value>
inline message::unpacker& operator>>(message::unpacker& u,
                                     std::pair<Key, Value>& v) {
  message::unpacker sub;
  u.iter_.recurse(sub.iter_);
  sub >> v.first;
  sub >> v.second;

  u.iter_.next();
  return u;
}

template <typename Element>
inline message::unpacker& operator>>(message::unpacker& u,
                                     std::vector<Element>& s) {
  message::unpacker sub;

  u.iter_.recurse(sub.iter_);
  auto arg_type = sub.iter_.get_arg_type();
  while (arg_type != DBUS_TYPE_INVALID) {
    s.emplace_back();
    sub >> s.back();
    arg_type = sub.iter_.get_arg_type();
  }
  u.iter_.next();
  return u;
}

inline std::ostream& operator<<(std::ostream& os, const message& m) {
  os << "type='" << m.get_type() << "',"
     << "sender='" << m.get_sender() << "',"
     << "interface='" << m.get_interface() << "',"
     << "member='" << m.get_member() << "',"
     << "path='" << m.get_path() << "',"
     << "destination='" << m.get_destination() << "'";
  return os;
}

}  // namespace dbus

#include <dbus/impl/message_iterator.ipp>

#endif  // DBUS_MESSAGE_HPP
