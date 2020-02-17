#pragma once
#include <iostream>
#include "IConnection.h"

class DefaultConnection : public IConnection
{
	bool m_connected;
	std::string m_address;

public:
	DefaultConnection(std::string address) : m_connected(false), m_address(address) {}

	bool Connect(HANDLE& connFile, bool async = false) override;
	bool Disconnect(HANDLE& connFile) override;
	[[nodiscard]] bool const IsConnected() noexcept override;
};

