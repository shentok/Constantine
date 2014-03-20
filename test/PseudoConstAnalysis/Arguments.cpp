// RUN: %clang_cc1 %s -fsyntax-only -verify

struct ComplexType
{
    int getValue();
    int getValue() const;

    int &getReference();
    const int &getReference() const;

    int &getReference2() const;

    int *getPointer();
    const int *getPointer() const;

    int *getPointer2() const;
};

void value2void(ComplexType &c) // expected-warning {{variable 'c' of type 'ComplexType &' could be declared with type 'const ComplexType &'}}
{
    c.getValue();
}

void reference2void(ComplexType &c) // expected-warning {{variable 'c' of type 'ComplexType &' could be declared with type 'const ComplexType &'}}
{
    c.getReference();
}

void reference2Value(ComplexType &c) // expected-warning {{variable 'c' of type 'ComplexType &' could be declared with type 'const ComplexType &'}}
{
    int i = c.getReference();
}

void reference2constReference(ComplexType &c) // expected-warning {{variable 'c' of type 'ComplexType &' could be declared with type 'const ComplexType &'}}
{
    const int &i = c.getReference();
}

void reference2reference(ComplexType &c)
{
    int &i = c.getReference();
}
