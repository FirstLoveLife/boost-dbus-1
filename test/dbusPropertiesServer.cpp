#include <dbus/connection.hpp>
#include <dbus/endpoint.hpp>
#include <dbus/filter.hpp>
#include <dbus/match.hpp>
#include <dbus/message.hpp>
#include <dbus/properties.hpp>
#include <functional>

#include <unistd.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

static const std::string dbus_boilerplate(
    "<!DOCTYPE node PUBLIC "
    "\"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/"
    "introspect.dtd\">\n");

TEST(DbusPropertiesInterface, EmptyObjectServer) {
  boost::asio::io_service io;
  auto system_bus = std::make_shared<dbus::connection>(io, dbus::bus::system);

  // Set up the object server, and send some test events
  dbus::DbusObjectServer foo(system_bus);

  EXPECT_EQ(foo.get_xml_for_path("/"), dbus_boilerplate + "<node></node>");
  EXPECT_EQ(foo.get_xml_for_path(""), dbus_boilerplate + "<node></node>");
}

TEST(DbusPropertiesInterface, BasicObjectServer) {
  boost::asio::io_service io;
  auto system_bus = std::make_shared<dbus::connection>(io, dbus::bus::system);

  // Set up the object server, and send some test events
  dbus::DbusObjectServer foo(system_bus);

  foo.register_object(std::make_shared<dbus::DbusObject>(
      system_bus, "/org/freedesktop/NetworkManager"));

  EXPECT_EQ(foo.get_xml_for_path("/"), dbus_boilerplate +
                                           "<node><node "
                                           "name=\"org\"></node></node>");
  EXPECT_EQ(foo.get_xml_for_path(""), dbus_boilerplate +
                                          "<node><node "
                                          "name=\"org\"></node></node>");

  EXPECT_EQ(foo.get_xml_for_path("/org"),
            dbus_boilerplate +
                "<node><node "
                "name=\"freedesktop\"></node></node>");
  EXPECT_EQ(foo.get_xml_for_path("/org/freedesktop"),
            dbus_boilerplate +
                "<node><node "
                "name=\"NetworkManager\"></node></node>");
  // TODO(Ed) turn this test back on once the signal interface stabilizes
  /*EXPECT_EQ(foo.get_xml_for_path("/org/freedesktop/NetworkManager"),
            dbus_boilerplate + "<node></node>");*/
}

TEST(DbusPropertiesInterface, SharedNodeObjectServer) {
  boost::asio::io_service io;
  auto system_bus = std::make_shared<dbus::connection>(io, dbus::bus::system);

  // Set up the object server, and send some test events
  dbus::DbusObjectServer foo(system_bus);

  foo.register_object(
      std::make_shared<dbus::DbusObject>(system_bus, "/org/freedesktop/test1"));
  foo.register_object(
      std::make_shared<dbus::DbusObject>(system_bus, "/org/freedesktop/test2"));

  EXPECT_EQ(foo.get_xml_for_path("/"), dbus_boilerplate +
                                           "<node><node "
                                           "name=\"org\"></node></node>");
  EXPECT_EQ(foo.get_xml_for_path(""), dbus_boilerplate +
                                          "<node><node "
                                          "name=\"org\"></node></node>");

  EXPECT_EQ(foo.get_xml_for_path("/org"),
            dbus_boilerplate +
                "<node><node "
                "name=\"freedesktop\"></node></node>");
  EXPECT_EQ(foo.get_xml_for_path("/org/freedesktop"),
            dbus_boilerplate +
                "<node><node "
                "name=\"test1\"></node><node name=\"test2\"></node></node>");
  // TODO(Ed) turn this test back on once the signal interface stabilizes
  /*
  EXPECT_EQ(foo.get_xml_for_path("/org/freedesktop/test1"),
            dbus_boilerplate + "<node></node>");
  EXPECT_EQ(foo.get_xml_for_path("/org/freedesktop/test2"),
            dbus_boilerplate + "<node></node>");
            */
}

TEST(LambdaDbusMethodTest, Basic) {
  bool lambda_called = false;
  auto lambda = [&](int32_t x) {
    EXPECT_EQ(x, 18);
    lambda_called = true;
    return std::make_tuple<int64_t, int32_t>(4L, 2);
  };
  boost::asio::io_service io;
  auto bus = std::make_shared<dbus::connection>(io, dbus::bus::session);
  auto dbus_method =
      dbus::LambdaDbusMethod<decltype(lambda)>("foo", bus, lambda);

  dbus::message m =
      dbus::message::new_call(dbus::endpoint("org.freedesktop.Avahi", "/",
                                             "org.freedesktop.Avahi.Server"),
                              "GetHostName");
  m.pack(static_cast<int32_t>(18));
  // Small thing that the dbus library normally does for us, but because we're
  // bypassing it, we need to fill it in as if it was done
  m.set_serial(585);
  dbus_method.call(m);
  EXPECT_EQ(lambda_called, true);
}

TEST(DbusPropertiesInterface, ObjectServer) {
  boost::asio::io_service io;
  auto bus = std::make_shared<dbus::connection>(io, dbus::bus::session);

  // Set up the object server, and send some test objects
  dbus::DbusObjectServer foo(bus);

  foo.register_object(
      std::make_shared<dbus::DbusObject>(bus, "/org/freedesktop/test1"));
  foo.register_object(
      std::make_shared<dbus::DbusObject>(bus, "/org/freedesktop/test2"));
  std::atomic<int> completion_count(0);

  std::array<std::string, 4> paths_to_test(
      {{"/", "/org", "/org/freedesktop", "/org/freedesktop/test1"}});

  for (auto& path : paths_to_test) {
    dbus::endpoint test_daemon(bus->get_unique_name(), path,
                               "org.freedesktop.DBus.Introspectable");
    dbus::message m = dbus::message::new_call(test_daemon, "Introspect");
    completion_count++;
    bus->async_send(
        m, [&](const boost::system::error_code ec, dbus::message r) {
          if (ec) {
            std::string error;
            r.unpack(error);
            FAIL() << ec << error;
          } else {
            std::string xml;
            r.unpack(xml);
            EXPECT_EQ(r.get_type(), "method_return");
            if (path == "/") {
              EXPECT_EQ(xml, dbus_boilerplate +
                                 "<node><node "
                                 "name=\"org\"></node></node>");
            } else if (path == "/org") {
              EXPECT_EQ(xml, dbus_boilerplate +
                                 "<node><node "
                                 "name=\"freedesktop\"></node></node>");
            } else if (path == "/org/freedesktop") {
              EXPECT_EQ(xml, dbus_boilerplate +
                                 "<node><node "
                                 "name=\"test1\"></node><node "
                                 "name=\"test2\"></node></node>");
            } else if (path == "/org/freedesktop/test1") {
            } else {
              FAIL() << "Unknown path: " << path;
            }
          }
          completion_count--;
          if (completion_count == 0) {
            io.stop();
          }
        });
  }
  io.run();
}

TEST(DbusPropertiesInterface, EmptyMethodServer) {
  boost::asio::io_service io;
  auto bus = std::make_shared<dbus::connection>(io, dbus::bus::session);

  // Set up the object server, and send some test objects
  dbus::DbusObjectServer foo(bus);
  foo.register_object(
      std::make_shared<dbus::DbusObject>(bus, "/org/freedesktop/test1"));

  dbus::endpoint test_daemon(bus->get_unique_name(), "/org/freedesktop/test1",
                             "org.freedesktop.DBus.Introspectable");
  dbus::message m = dbus::message::new_call(test_daemon, "Introspect");

  bus->async_send(m, [&](const boost::system::error_code ec, dbus::message r) {
    if (ec) {
      std::string error;
      r.unpack(error);
      FAIL() << ec << error;
    } else {
      std::cout << r;
      std::string xml;
      r.unpack(xml);
      EXPECT_EQ(r.get_type(), "method_return");
      // TODO(ed) turn back on when method interface stabilizes
      // EXPECT_EQ(xml, dbus_boilerplate + "<node></node>");
    }

    io.stop();

  });

  io.run();
}

TEST(DbusPropertiesInterface, MethodServer) {
  boost::asio::io_service io;
  auto bus = std::make_shared<dbus::connection>(io, dbus::bus::session);

  // Set up the object server, and send some test objects
  dbus::DbusObjectServer foo(bus);
  auto object =
      std::make_shared<dbus::DbusObject>(bus, "/org/freedesktop/test1");
  foo.register_object(object);

  auto iface = std::make_shared<dbus::DbusInterface>(
      "org.freedesktop.My.Interface", bus);
  object->register_interface(iface);

  iface->register_method("MyMethod", [](uint32_t x) {

    std::cout << "method called.  Got:" << x << "\n";
    return std::make_tuple<int>(42);
  });

    iface->register_method("VoidMethod", []() {

    std::cout << "method called.\n";
    return std::make_tuple<int>(42);
  });


  dbus::endpoint test_daemon(bus->get_unique_name(), "/org/freedesktop/test1",
                             "org.freedesktop.DBus.Introspectable");
  dbus::message m = dbus::message::new_call(test_daemon, "Introspect");

  bus->async_send(m, [&](const boost::system::error_code ec, dbus::message r) {
    if (ec) {
      std::string error;
      r.unpack(error);

      FAIL() << ec << error;
    } else {
      std::string xml;
      r.unpack(xml);
      EXPECT_EQ(r.get_type(), "method_return");
      // todo(ED)
      /*
      EXPECT_EQ(xml, dbus_boilerplate +
                         "<node><interface "
                         "name=\"MyInterface\"><method "
                         "name=\"MyMethod\"></method></interface></node>");
                         */
    }
    io.stop();
  });

  io.run();
}

TEST(DbusPropertiesInterface, PropertiesInterface) {
  boost::asio::io_service io;
  auto bus = std::make_shared<dbus::connection>(io, dbus::bus::session);

  // Set up the object server, and send some test objects
  dbus::DbusObjectServer foo(bus);
  auto object =
      std::make_shared<dbus::DbusObject>(bus, "/org/freedesktop/test1");
  foo.register_object(object);

  auto iface = std::make_shared<dbus::DbusInterface>(
      "org.freedesktop.My.Interface", bus);
  object->register_interface(iface);

  iface->set_property("foo", (uint32_t)26);

  dbus::endpoint test_daemon(bus->get_unique_name(), "/org/freedesktop/test1",
                             "org.freedesktop.DBus.Properties");
  dbus::message m = dbus::message::new_call(test_daemon, "Get");
  m.pack("org.freedesktop.My.Interface", "foo");

  bus->async_send(m, [&](const boost::system::error_code ec, dbus::message r) {
    if (ec) {
      std::string error;
      r.unpack(error);

      FAIL() << ec << error;
    } else {
      std::cout << r;

      EXPECT_EQ(r.get_type(), "method_return");

      dbus::dbus_variant value;
      r.unpack(value);

      EXPECT_EQ(boost::get<uint32_t>(value), 26);
    }
    io.stop();
  });

  io.run();
}