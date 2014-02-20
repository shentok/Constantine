// RUN: %clang_cc1 %usage %s -fsyntax-only -verify

class Bar {
public:
    int getValueAsValue();

    int &getValueAsReference();

    int *getValueAsPointer();

private:
    int m_value;
};


int Bar::getValueAsValue() {
    return m_value; // expected-note {{symbol 'm_value' was used with type 'int'}}
}

int &Bar::getValueAsReference() {
    return m_value; // expected-note {{symbol 'm_value' was used with type 'int &'}}
}

int *Bar::getValueAsPointer() {
    return &m_value; // expected-note {{symbol 'm_value' was used with type 'int *'}}
}
