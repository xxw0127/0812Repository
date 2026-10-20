QT += core gui widgets testlib sql
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = client_tests

INCLUDEPATH += $$PWD/..
RESOURCES += ../resources/resources.qrc
SOURCES += \
    test_client_core.cpp \
    ../core/app_settings.cpp \
    ../core/autostart.cpp \
    ../core/client_policy.cpp \
    ../core/client_log.cpp \
    ../core/message_time.cpp \
    ../core/message_store.cpp \
    ../core/outbox.cpp \
    ../core/message_renderer.cpp \
    ../core/contact_store.cpp \
    ../core/avatar_utils.cpp \
    ../core/avatar_cache.cpp \
    ../core/history_store.cpp \
    ../ui/chat_view.cpp \
    ../ui/message_action_bar.cpp \
    ../ui/profile_dialog.cpp \
    ../ui/login_page.cpp \
    ../ui/settings_dialog.cpp
HEADERS += \
    ../core/app_settings.h \
    ../core/autostart.h \
    ../core/client_policy.h \
    ../core/client_log.h \
    ../core/chat_message.h \
    ../core/message_time.h \
    ../core/message_store.h \
    ../core/outbox.h \
    ../core/message_renderer.h \
    ../core/ui_theme.h \
    ../core/contact_store.h \
    ../core/avatar_utils.h \
    ../core/avatar_cache.h \
    ../core/history_store.h \
    ../core/user_profile.h \
    ../ui/chat_view.h \
    ../ui/message_action_bar.h \
    ../ui/profile_dialog.h \
    ../ui/login_page.h \
    ../ui/settings_dialog.h
