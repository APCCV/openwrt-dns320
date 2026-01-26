include $(TOPDIR)/rules.mk

PKG_NAME:=dns320-fand
PKG_VERSION:=1.0
PKG_RELEASE:=1

SOURCE_DIR:=./files

PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

define Package/dns320-fand
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=DNS-320 A1 fan control daemon
  DEPENDS:=@TARGET_kirkwood
endef

define Package/dns320-fand/description
  D-Link DNS-320 A1 daemon to control fan based on CPU and HDD temperatures
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp $(SOURCE_DIR)/dns320-fand.c $(PKG_BUILD_DIR)
	$(Build/Patch)
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) -o $(PKG_BUILD_DIR)/dns320-fand.o -c $(PKG_BUILD_DIR)/dns320-fand.c
	$(TARGET_CC) $(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/$(1) $(PKG_BUILD_DIR)/dns320-fand.o
endef

define Package/dns320-fand/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dns320-fand $(1)/usr/sbin
	$(INSTALL_BIN) ./files/dns320-fand.init $(1)/etc/init.d/dns320-fand
	$(INSTALL_BIN) ./files/dns320-fand.conf $(1)/etc/config/dns320-fand
endef

$(eval $(call BuildPackage,dns320-fand))
