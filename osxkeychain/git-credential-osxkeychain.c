
#include <credential_helper.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <Security/Security.h>

static SecProtocolType protocol;
static UInt16 port;

#define KEYCHAIN_ITEM(x) (x ? strlen(x) : 0), x
#define KEYCHAIN_ARGS(c) \
	NULL, /* default keychain */ \
	KEYCHAIN_ITEM(c->host), \
	0, NULL, /* account domain */ \
	KEYCHAIN_ITEM(c->username), \
	KEYCHAIN_ITEM(c->path), \
	port, \
	protocol, \
	kSecAuthenticationTypeDefault

static inline char *xstrndup(const char *str, size_t len)
{
	char *ret = strndup(str,len);
	if (!ret)
		die_errno(errno);

	return ret;
}

static int prepare_internet_password(struct credential *c)
{
	char *colon;

	if (!c->protocol || !c->host)
		return -1;

	if (!strcmp(c->protocol, "https"))
		protocol = kSecProtocolTypeHTTPS;
	else if (!strcmp(c->protocol, "http"))
		protocol = kSecProtocolTypeHTTP;
	else /* we don't yet handle other protocols */
		return -1;

	colon = strchr(c->host, ':');
	if (colon) {
		*colon++ = '\0';
		port = atoi(colon);
	}
	return 0;
}

static void
find_username_in_item(SecKeychainItemRef item, struct credential *c)
{
	SecKeychainAttributeList list;
	SecKeychainAttribute attr;

	list.count = 1;
	list.attr = &attr;
	attr.tag = kSecAccountItemAttr;

	if (SecKeychainItemCopyContent(item, NULL, &list, NULL, NULL))
		return;

	free(c->username);
	c->username = xstrndup(attr.data, attr.length);

	SecKeychainItemFreeContent(&list, NULL);
}

static int find_internet_password(struct credential *c)
{
	void *buf;
	UInt32 len;
	SecKeychainItemRef item;

	/* Silently ignore unsupported protocols */
	if (prepare_internet_password(c))
		return EXIT_SUCCESS;

	if (SecKeychainFindInternetPassword(KEYCHAIN_ARGS(c), &len, &buf, &item))
		return EXIT_SUCCESS;

	free(c->password);
	c->password = xstrndup(buf, len);

	if (!c->username)
		find_username_in_item(item);

	SecKeychainItemFreeContent(NULL, buf);
}

static int delete_internet_password(struct credential *c)
{
	SecKeychainItemRef item;

	/*
	 * Require at least a protocol and host for removal, which is what git
	 * will give us; if you want to do something more fancy, use the
	 * Keychain manager.
	 */
	if (!c->protocol || !c->host)
		return EXIT_FAILURE;

	/* Silently ignore unsupported protocols */
	if (prepare_internet_password(c))
		return EXIT_SUCCESS;

	if (SecKeychainFindInternetPassword(KEYCHAIN_ARGS(c), 0, NULL, &item))
		return EXIT_SUCCESS;

	SecKeychainItemDelete(item);
}

static int add_internet_password(struct credential *c)
{
	/* Only store complete credentials */
	if (!c->protocol || !c->host || !c->username || !c->password)
		return EXIT_FAILURE;

	if (prepare_internet_password(c))
		return EXIT_FAILURE;

	if (SecKeychainAddInternetPassword(
	      KEYCHAIN_ARGS(c),
	      KEYCHAIN_ITEM(password),
	      NULL))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

/*
 * Table with helper operation callbacks, used by generic
 * credential helper main function.
 */
struct credential_operation const credential_helper_ops[] =
{
	{ "get",   find_internet_password   },
	{ "store", add_internet_password    },
	{ "erase", delete_internet_password },
	CREDENTIAL_OP_END
};
