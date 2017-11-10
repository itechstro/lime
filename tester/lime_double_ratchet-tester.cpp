/*
	lime_double_ratchet-tester.cpp
	Copyright (C) 2017  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define BCTBX_LOG_DOMAIN "lime-tester"
#include <bctoolbox/logging.h>

#include "lime-tester.hpp"
#include "lime-tester-utils.hpp"
#include "lime_localStorage.hpp"

#include <bctoolbox/tester.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "bctoolbox/crypto.h"


using namespace::std;
using namespace::lime;

static const std::string AD{"alice@sip.linphone.org;opaque=user:epid:UghFocauauCHBHoLhAAA;gruu;bob@sip.linphone.org;opaque=user:epid:CTboShduoSCdauSEUDbka;gruu;"};

/*****************************************************************************/
/* Direct use of DR encrypt/decrypt, is not what is done in real usage       */
/*****************************************************************************/

/**
  * @param[in]	period		altern sended each <period> messages (sequence will anyways always start with alice send - bob receive - bob send)
  * @param[in]	skip_period	same than above but for reception skipping: at each begining of skip_period, skip reception of skip_length messages
  * @param[in]	skip_length	see previous: number of messages to be skipped
  * @param[in]	skip_delay	number of messages sending before the skip messages are received
  *				ex: if message 5 is skipped and skip_delay is 10, message 5 will be received after message 15 was sent - and may be received
  *				All delayed messaged are received in their order of sending at the end of message stack processing
  */
template <typename Curve>
static void dr_skippedMessages_basic_test(const uint8_t period=1, const uint8_t skip_period=1000, const uint8_t skip_length=0, const uint8_t skip_delay=0, const std::string db_filename="dr_skipped_message_basic_tmp") {
	std::shared_ptr<DR<Curve>> alice, bob;
	std::shared_ptr<lime::Db> aliceLocalStorage, bobLocalStorage;
	std::string aliceFilename(db_filename);
	std::string bobFilename(db_filename);
	aliceFilename.append(".alice.sqlite3");
	bobFilename.append(".bob.sqlite3");

	//clean tmp files
	remove(aliceFilename.data());
	remove(bobFilename.data());

	// create sessions
	dr_sessionsInit(alice, bob, aliceLocalStorage, bobLocalStorage, aliceFilename, bobFilename);
	std::vector<std::vector<uint8_t>> cipher;
	std::vector<std::vector<recipientInfos<Curve>>> recipients;
	std::vector<uint8_t> messageSender; // hold status of message: 0 not sent, 1 sent by Alice, 2 sent by Bob, 3 received
	std::vector<std::string> plainMessage;

	// resize vectors to hold all materials
	cipher.resize(lime_messages_pattern.size());
	recipients.resize(lime_messages_pattern.size());
	plainMessage.resize(lime_messages_pattern.size());
	messageSender.resize(lime_messages_pattern.size(), 0);

	bool aliceSender=true;
	bctbx_debug("Start skip test\n\n");
	for (size_t i=0; i<lime_messages_pattern.size(); i++) {
		/* sending */
		if (aliceSender) {
			// alice encrypt a message
			recipients[i].emplace_back("bob",alice);
			std::vector<uint8_t> plaintext{lime_messages_pattern[i].begin(), lime_messages_pattern[i].end()};
			std::vector<uint8_t> cipherMessage{};
			encryptMessage(recipients[i], plaintext, "bob", "alice", cipher[i]);
			bctbx_debug("alice encrypt %d", int(i));

			messageSender[i] = 1;
			if (i%period == 0) {
				aliceSender=false;
			}
		} else {
			// bob encrypt a message
			recipients[i].emplace_back("alice",bob);
			std::vector<uint8_t> plaintext{lime_messages_pattern[i].begin(), lime_messages_pattern[i].end()};
			std::vector<uint8_t> cipherMessage{};
			encryptMessage(recipients[i], plaintext, "alice", "bob", cipher[i]);
			bctbx_debug("bob encrypt %d", int(i));

			messageSender[i] = 2;
			if (i%period == 0) {
				aliceSender=true;
			}
		}

		/* receiving (or later): immediate reception is skipped for skip_length messages eack skip_period messages */
		if ((i==0) || !(i%skip_period<skip_length)) { // do not skip the first message otherwise bob wont be able to write to alice
			if (messageSender[i]==2) {
				bctbx_debug("alice decrypt %d", int(i));
				// alice decrypt it
				std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
				recipientDRSessions.push_back(alice);
				std::vector<uint8_t> plainBuffer{};
				decryptMessage("bob", "alice", "alice", recipientDRSessions, recipients[i][0].cipherHeader, cipher[i], plainBuffer);
				plainMessage[i] = std::string{plainBuffer.begin(), plainBuffer.end()};

				messageSender[i]=3;
			} else if (messageSender[i]==1) {
				bctbx_debug("bob decrypt %d", int(i));
				// bob decrypt it
				std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
				recipientDRSessions.push_back(bob);
				std::vector<uint8_t> plainBuffer{};
				decryptMessage("alice", "bob", "bob", recipientDRSessions, recipients[i][0].cipherHeader, cipher[i], plainBuffer);
				plainMessage[i] = std::string{plainBuffer.begin(), plainBuffer.end()};

				messageSender[i]=3;
			} else {
				BC_FAIL("That should never happend, something is wrong in the test not the lib");
			}
		}

		/* Do we have some old message to decrypt */
		if (i>=skip_delay) {
			for (size_t j=0; j<i-skip_delay; j++) {
				if (messageSender[j]==2) {
					bctbx_debug("alice decrypt %d", int(j));
					// alice decrypt it
					std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
					recipientDRSessions.push_back(alice);
					std::vector<uint8_t> plainBuffer{};
					decryptMessage("bob", "alice", "alice", recipientDRSessions, recipients[j][0].cipherHeader, cipher[j], plainBuffer);
					plainMessage[j] = std::string{plainBuffer.begin(), plainBuffer.end()};

					messageSender[j]=3;
				} else if (messageSender[j]==1) {
					bctbx_debug("bob decrypt %d", int(j));
					// bob decrypt it
					std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
					recipientDRSessions.push_back(bob);
					std::vector<uint8_t> plainBuffer{};
					decryptMessage("alice", "bob", "bob", recipientDRSessions, recipients[j][0].cipherHeader, cipher[j], plainBuffer);
					plainMessage[j] = std::string{plainBuffer.begin(), plainBuffer.end()};

					messageSender[j]=3;
				}
			}
		}

	}

	/* Do we have some old message to decrypt(ignore delay we're at the end of test */
	for (size_t j=0; j<lime_messages_pattern.size(); j++) {
		if (messageSender[j]==2) {
			bctbx_debug("alice decrypt %d", int(j));
			// alice decrypt it
			std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
			recipientDRSessions.push_back(alice);
			std::vector<uint8_t> plainBuffer{};
			decryptMessage("bob", "alice", "alice", recipientDRSessions, recipients[j][0].cipherHeader, cipher[j], plainBuffer);
			plainMessage[j] = std::string{plainBuffer.begin(), plainBuffer.end()};

			messageSender[j]=3;
		} else if (messageSender[j]==1) {
			bctbx_debug("bob decrypt %d", int(j));
			// bob decrypt it
			std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
			recipientDRSessions.push_back(bob);
			std::vector<uint8_t> plainBuffer{};
			decryptMessage("alice", "bob", "bob", recipientDRSessions, recipients[j][0].cipherHeader, cipher[j], plainBuffer);
			plainMessage[j] = std::string{plainBuffer.begin(), plainBuffer.end()};

			messageSender[j]=3;
		}
	}

	// same same
	for (size_t i=0; i<lime_messages_pattern.size(); i++) {
		BC_ASSERT_TRUE(plainMessage[i] == lime_messages_pattern[i]);
	}
	bctbx_debug("End of skip test\n\n");
}

static void dr_skippedMessages_basic(void) {
#ifdef EC25519_ENABLED
	/* send batch of 10 messages, delay by 15 one message each time we reach the end of the batch*/
	dr_skippedMessages_basic_test<C255>(10, 10, 1, 15, "dr_skipMessage_1_X25519");
	/* delayed messages covering more than a bath */
	dr_skippedMessages_basic_test<C255>(3, 7, 4, 17, "dr_skipMessage_2_X25519");
#endif
#ifdef EC448_ENABLED
	dr_skippedMessages_basic_test<C448>(10, 10, 1, 15, "dr_skipMessage_1_X448");
	dr_skippedMessages_basic_test<C448>(5, 5, 1, 10, "dr_skipMessage_2_X448");
#endif
}

/* alice send <period> messages to bob, and bob replies with <period> messages and so on until the end of message pattern list  */
template <typename Curve>
static void dr_long_exchange_test(uint8_t period=1, std::string db_filename="dr_long_exchange_tmp") {
	std::shared_ptr<DR<Curve>> alice, bob;
	std::shared_ptr<lime::Db> aliceLocalStorage, bobLocalStorage;
	std::string aliceFilename(db_filename);
	std::string bobFilename(db_filename);
	aliceFilename.append(".alice.sqlite3");
	bobFilename.append(".bob.sqlite3");
	// create sessions
	dr_sessionsInit(alice, bob, aliceLocalStorage, bobLocalStorage, aliceFilename, bobFilename);
	std::vector<uint8_t> aliceCipher, bobCipher;

	bool aliceSender=true;

	for (size_t i=0; i<lime_messages_pattern.size(); i++) {
		if (aliceSender) {
			// alice encrypt a message
			std::vector<recipientInfos<Curve>> recipients;
			recipients.emplace_back("bob",alice);
			std::vector<uint8_t> plaintext{lime_messages_pattern[i].begin(), lime_messages_pattern[i].end()};
			std::vector<uint8_t> cipherMessage{};
			encryptMessage(recipients, plaintext, "bob", "alice", cipherMessage);

			// bob decrypt it
			std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
			recipientDRSessions.push_back(bob);
			std::vector<uint8_t> plainBuffer{};
			decryptMessage("alice", "bob", "bob", recipientDRSessions, recipients[0].cipherHeader, cipherMessage, plainBuffer);
			std::string plainMessage{plainBuffer.begin(), plainBuffer.end()};

			// same same?
			BC_ASSERT_TRUE(plainMessage==lime_messages_pattern[i]);
			if (i%period == 0) {
				aliceSender=false;
				/* destroy and reload bob sessions */
				auto bobSessionId=bob->dbSessionId();
				bob = nullptr; // release and destroy bob DR context
				bob = make_shared<DR<Curve>>(bobLocalStorage.get(), bobSessionId);
			}
		} else {
			// bob replies
			std::vector<recipientInfos<Curve>> recipients;
			recipients.emplace_back("alice",bob);
			std::vector<uint8_t> plaintext{lime_messages_pattern[i].begin(), lime_messages_pattern[i].end()};
			std::vector<uint8_t> cipherMessage{};
			encryptMessage(recipients, plaintext, "alice", "bob", cipherMessage);

			// alice decrypt it
			std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
			recipientDRSessions.push_back(alice);
			std::vector<uint8_t> plainBuffer{};
			decryptMessage("bob", "alice", "alice", recipientDRSessions, recipients[0].cipherHeader, cipherMessage, plainBuffer);
			std::string plainMessage{plainBuffer.begin(), plainBuffer.end()};

			// same same?
			BC_ASSERT_TRUE(plainMessage==lime_messages_pattern[i]);
			if (i%period == 0) {
				aliceSender=true;
				/* destroy and reload alice sessions */
				auto aliceSessionId=alice->dbSessionId();
				alice = nullptr; // release and destroy alice DR context
				alice = make_shared<DR<Curve>>(aliceLocalStorage.get(), aliceSessionId);
			}
		}
	}

	// remove temporary db file
	remove(aliceFilename.data());
	remove(bobFilename.data());
}
static void dr_long_exchange1(void) {
#ifdef EC25519_ENABLED
	dr_long_exchange_test<C255>(1, "dr_long_exchange_1_X25519");
#endif
#ifdef EC448_ENABLED
	dr_long_exchange_test<C448>(1, "dr_long_exchange_1_X448");
#endif
}
static void dr_long_exchange3(void) {
#ifdef EC25519_ENABLED
	dr_long_exchange_test<C255>(3, "dr_long_exchange_3_X25519");
#endif
#ifdef EC448_ENABLED
	dr_long_exchange_test<C448>(3, "dr_long_exchange_3_X448");
#endif
}
static void dr_long_exchange10(void) {
#ifdef EC25519_ENABLED
	dr_long_exchange_test<C255>(10, "dr_long_exchange_10_X25519");
#endif
#ifdef EC448_ENABLED
	dr_long_exchange_test<C448>(10, "dr_long_exchange_10_X448");
#endif
}

/* alice send a message to bob, and he replies */
template <typename Curve>
static void dr_basic_test(std::string db_filename) {
	std::shared_ptr<DR<Curve>> alice, bob;
	std::shared_ptr<lime::Db> localStorageAlice, localStorageBob;
	std::string aliceFilename(db_filename);
	std::string bobFilename(db_filename);
	aliceFilename.append(".alice.sqlite3");
	bobFilename.append(".bob.sqlite3");

	// remove temporary db file if they are here
	remove(aliceFilename.data());
	remove(bobFilename.data());

	// create sessions: alice sender, bob receiver
	dr_sessionsInit(alice, bob, localStorageAlice, localStorageBob, aliceFilename, bobFilename);
	std::vector<uint8_t> aliceCipher, bobCipher;

	// alice encrypt a message
	std::vector<recipientInfos<Curve>> recipients;
	recipients.emplace_back("bob",alice);
	std::vector<uint8_t> plaintextAlice{lime_messages_pattern[0].begin(), lime_messages_pattern[0].end()};
	encryptMessage(recipients, plaintextAlice, "bob", "alice", aliceCipher);

	// bob decrypt it
	std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
	recipientDRSessions.push_back(bob);
	std::vector<uint8_t> plainBuffer{};
	decryptMessage("alice", "bob", "bob", recipientDRSessions, recipients[0].cipherHeader, aliceCipher, plainBuffer);
	std::string plainMessageBob{plainBuffer.begin(), plainBuffer.end()};

	// same same?
	BC_ASSERT_TRUE(plainMessageBob==lime_messages_pattern[0]);


	// bob replies
	recipients.clear();
	recipients.emplace_back("alice",bob);
	std::vector<uint8_t> plaintextBob{lime_messages_pattern[1].begin(), lime_messages_pattern[1].end()};
	encryptMessage(recipients, plaintextBob, "alice", "bob", bobCipher);

	// alice decrypt it
	recipientDRSessions.clear();
	recipientDRSessions.push_back(alice);
	plainBuffer.clear();
	decryptMessage("bob", "alice", "alice", recipientDRSessions, recipients[0].cipherHeader, bobCipher, plainBuffer);
	std::string plainMessageAlice{plainBuffer.begin(), plainBuffer.end()};

	// same same?
	BC_ASSERT_TRUE(plainMessageAlice==lime_messages_pattern[1]);
}

static void dr_basic(void) {
#ifdef EC25519_ENABLED
	dr_basic_test<C255>("dr_basic_X25519");
#endif
#ifdef EC448_ENABLED
	dr_basic_test<C448>("dr_basic_X448");
#endif
}

/*****************************************************************************/
/*                                                                           */
/* Use the encrypt/decrypt message functions: encrypt for multiple recipients*/
/* This is the API to be used even in case of single recipient               */
/*                                                                           */
/*****************************************************************************/

/* alice send a message to bob, and he replies */
template <typename Curve>
static void multidevice_basic_test(std::string db_filename) {
	/* we have 2 users "alice" and "bob" with 3 devices each */
	std::vector<std::string> usernames{"alice", "bob"};
	std::vector<std::vector<std::vector<std::vector<sessionDetails<Curve>>>>> users;

	/* give correct size to our users vector for users and devices count */
	users.resize(usernames.size());
	for (auto &user : users) user.resize(3);

	/* init and instanciate, session will be then found in a 4 dimensional vector indexed this way : [self user id][self device id][peer user id][peer device id] */
	dr_devicesInit(db_filename, users, usernames);

	/* Send a message from alice.dev0 to all bob device(and copy to alice devices too) */
	std::vector<recipientInfos<Curve>> recipients;
	for (size_t u=0; u<users.size(); u++) { // loop users
		for (size_t d=0; d<users[u].size(); d++) { // devices
			if (u!=0 || d!=0) { // sender is users 0, device 0, do not encode for him
				recipientInfos<Curve> recipient;
				recipient.deviceId = users[0][0][u][d].peer_userId; // source is 0,0 dest is u,d
				recipient.deviceId.append("@").append(std::to_string(users[0][0][u][d].peer_deviceIndex)); //deviceId is peerUserId@peerDeviceId
				recipient.DRSession = users[0][0][u][d].DRSession;
				recipients.push_back(recipient);
			}
		}
	}

	std::string sourceId = usernames[0];
	sourceId.append("@").append(to_string(0)); // source deviceId shall be alice@0
	std::vector<std::vector<uint8_t>> cipherHeader;
	std::vector<uint8_t> cipherMessage;
	std::vector<uint8_t> plaintext{lime_messages_pattern[0].begin(), lime_messages_pattern[0].end()};

	encryptMessage(recipients, plaintext, usernames[1], sourceId, cipherMessage);

	// Now try decrypt the messages received on every device
	// pop the headers from front in recipients so loop on the exact same order as when building it
	for (size_t u=0; u<users.size(); u++) { // loop users
		for (size_t d=0; d<users[u].size(); d++) { // devices
			if (u!=0 || d!=0) { // sender is users 0, device 0, do not decode with it
				recipientInfos<Curve> recipient = recipients.front();
				// store our DRSession in a vector as interface request a vector of DRSession to try them all, we may have several sessions with one peer device
				std::vector<shared_ptr<DR<Curve>>> recipientDRSessions{};
				recipientDRSessions.push_back(users[u][d][0][0].DRSession); // we are u,d receiving from 0,0

				std::vector<uint8_t> plaintext_back;
				decryptMessage(sourceId, recipient.deviceId, usernames[1], recipientDRSessions, recipient.cipherHeader, cipherMessage, plaintext_back); // recipient id is username 1

				// convert back the output vector to a string
				std::string plaintext_back_string{plaintext_back.begin(), plaintext_back.end()};

				BC_ASSERT_TRUE(plaintext_back_string==lime_messages_pattern[0]);

				recipients.erase(recipients.begin());
			}
		}
	}
}

static void multidevice_basic(void) {
#ifdef EC25519_ENABLED
	multidevice_basic_test<C255>("ed_basic_C25519");
#endif
#ifdef EC448_ENABLED
	multidevice_basic_test<C448>("ed_basic_C448");
#endif
}
static test_t tests[] = {
	TEST_NO_TAG("DR Basic", dr_basic),
	TEST_NO_TAG("DR Long Exchange 1", dr_long_exchange1),
	TEST_NO_TAG("DR Long Exchange 3", dr_long_exchange3),
	TEST_NO_TAG("DR Long Exchange 10", dr_long_exchange10),
	TEST_NO_TAG("DR Skip message", dr_skippedMessages_basic),
	TEST_NO_TAG("Multidevices Basic", multidevice_basic),
};

test_suite_t lime_double_ratchet_test_suite = {
	"Double Ratchet",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};