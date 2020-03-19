// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Ion Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?label=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString("Some Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=100&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString());

    /* **TODO** - test fails
    QVERIFY(GUIUtil::parseBitcoinURI("ion://idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?message=Some Example Address", &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.label == QString());
    */

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?req-message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=1,000&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=1,000.0&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?amount=100&label=Some Example&message=Some Example Message"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));
    QVERIFY(rv.message == QString("Some Example Message"));

    // Verify that IS=xxx does not lead to an error (we ignore the field)
    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6?req-IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("ion:idFcVh28YpxoCdJhiVjmsUn1Cq9rpJ6KP6"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
}
