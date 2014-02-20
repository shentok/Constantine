// RUN: %clang_cc1 %s -fsyntax-only -verify

class Bar {
    int m_value;

    int getValueAsValue();

    int &getValueAsReference();

    int *getValueAsPointer();
};


int Bar::getValueAsValue() { // expected-warning {{function 'getValueAsValue' could be declared as const}}
    return m_value;
}

int &Bar::getValueAsReference() {
    return m_value;
}

int *Bar::getValueAsPointer() {
    return &m_value;
}
