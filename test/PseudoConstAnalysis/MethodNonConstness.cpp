// RUN: %clang_cc1 %s -fsyntax-only -verify

class Value {
    int m_value;

    int getValue();

    int &getReference();

    int const &getReferenceConst();

    int *getPointer();

    int const *getPointerConst();
};


int Value::getValue() { // expected-warning {{function 'getValue' could be declared as const}}
    return m_value;
}

int &Value::getReference() {
    return m_value;
}

const int &Value::getReferenceConst() // expected-warning {{function 'getReferenceConst' could be declared as const}}
{
    return m_value;
}

int *Value::getPointer() {
    return &m_value;
}

int const *Value::getPointerConst() // expected-warning {{function 'getPointerConst' could be declared as const}}
{
    return &m_value;
}


class Pointer {
    int *m_pointer;

    void setPointer(int *const pointer);

    int getValue();

    int &getReference();

    int const &getReferenceConst();

    int *getPointer();

    int const *getPointerConst();
};


void Pointer::setPointer(int *const pointer)
{
    m_pointer = pointer;
}

int Pointer::getValue() // expected-warning {{function 'getValue' could be declared as const}}
{
    return *m_pointer;
}

int &Pointer::getReference()
{
    return *m_pointer;
}

const int &Pointer::getReferenceConst() // expected-warning {{function 'getReferenceConst' could be declared as const}}
{
    return *m_pointer;
}

int *Pointer::getPointer()
{
    return m_pointer;
}

const int *Pointer::getPointerConst() // expected-warning {{function 'getPointerConst' could be declared as const}}
{
    return m_pointer;
}

class ConstPointer {
    const int * m_pointer;

public:
    void setPointer( int * foo );
};


void ConstPointer::setPointer(int *foo)
{
    m_pointer = foo;
}
