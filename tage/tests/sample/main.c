#include "unity.h"

// Functions under test — typically these live in a separate .c file
// and are declared via a header.
int mult(int a, int b) { return a * b; }
int add(int a, int b)  { return a + b; }


// setUp / tearDown are called by Unity before / after EVERY test.
void setUp(void)    { /* initialise shared state here */ }
void tearDown(void) { /* release shared state here    */ }


/* -------------------------------------------------------------------------
 * Naming convention:  test_<functionName>_<condition>_<expectedOutcome>
 *
 * Examples:
 *   test_add_positiveOperands_returnsSum
 *   test_mult_byZero_returnsZero
 *   test_lookup_coldCache_predictsNotTaken
 * ------------------------------------------------------------------------- */

void test_add_positiveOperands_returnsSum(void)
{
    TEST_ASSERT_EQUAL_INT(5, add(2, 3));
}

void test_mult_positiveOperands_returnsProduct(void)
{
    // _MESSAGE variants attach a context string to the failure output.
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, mult(2, 5),
        "2 * 5 should equal 10");
}

void test_add_largeValues_doesNotOverflow(void)
{
    // can log message unconditionally
    TEST_MESSAGE("Checking add with values near INT_MAX/2");
    TEST_ASSERT_EQUAL_INT(100000, add(40000, 60000));
}

void test_mult_negativeOperands_returnsPositive(void)
{
    // TEST_IGNORE_MESSAGE marks the test as IGNORED (not a failure).
    // Use this for known-incomplete or not-yet-implemented behaviour.
    TEST_IGNORE_MESSAGE("TODO: verify sign handling once implemented");
}

//intentionally failing test, to test failing behavior
void test_add_wrongExpectation_demonstratesFailureOutput(void)
{
    // This test is here to show what a Unity failure message looks like.
    // The _MESSAGE variant makes the reason explicit in the output.
    TEST_ASSERT_EQUAL_INT_MESSAGE(11, add(2, 3),
        "Expected wrong value on purpose to demo failure output");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_add_positiveOperands_returnsSum);
    RUN_TEST(test_mult_positiveOperands_returnsProduct);
    RUN_TEST(test_add_largeValues_doesNotOverflow);
    RUN_TEST(test_mult_negativeOperands_returnsPositive);   // IGNORED
    RUN_TEST(test_add_wrongExpectation_demonstratesFailureOutput); // FAIL

    return UNITY_END();
}
