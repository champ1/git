#!/bin/sh

test_description='signed commit tests'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed commits' '
	test_when_finished "test_unconfig commit.gpgsign" &&

	echo 1 >file && git add file &&
	test_tick && git commit -S -m initial &&
	git tag initial &&
	git branch side &&

	echo 2 >file && test_tick && git commit -a -S -m second &&
	git tag second &&

	git checkout side &&
	echo 3 >elif && git add elif &&
	test_tick && git commit -m "third on side" &&

	git checkout master &&
	test_tick && git merge -S side &&
	git tag merge &&

	echo 4 >file && test_tick && git commit -a -m "fourth unsigned" &&
	git tag fourth-unsigned &&

	test_tick && git commit --amend -S -m "fourth signed" &&
	git tag fourth-signed &&

	git config commit.gpgsign true &&
	echo 5 >file && test_tick && git commit -a -m "fifth signed" &&
	git tag fifth-signed &&

	git config commit.gpgsign false &&
	echo 6 >file && test_tick && git commit -a -m "sixth" &&
	git tag sixth-unsigned &&

	git config commit.gpgsign true &&
	echo 7 >file && test_tick && git commit -a -m "seventh" --no-gpg-sign &&
	git tag seventh-unsigned &&

	test_tick && git rebase -f HEAD^^ && git tag sixth-signed HEAD^ &&
	git tag seventh-signed
'

test_expect_success GPG 'show signatures' '
	(
		for commit in initial second merge fourth-signed fifth-signed sixth-signed master
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual || exit 1
			! grep "BAD signature from" actual || exit 1
			echo $commit OK
		done
	) &&
	(
		for commit in merge^2 fourth-unsigned sixth-unsigned seventh-unsigned
		do
			git show --pretty=short --show-signature $commit >actual &&
			grep "Good signature from" actual && exit 1
			! grep "BAD signature from" actual || exit 1
			echo $commit OK
		done
	)
'

test_expect_success GPG 'detect fudged signature' '
	git cat-file commit master >raw &&

	sed -e "s/seventh/7th forged/" raw >forged1 &&
	git hash-object -w -t commit forged1 >forged1.commit &&
	git show --pretty=short --show-signature $(cat forged1.commit) >actual1 &&
	grep "BAD signature from" actual1 &&
	! grep "Good signature from" actual1
'

test_expect_success GPG 'detect fudged signature with NUL' '
	git cat-file commit master >raw &&
	cat raw >forged2 &&
	echo Qwik | tr "Q" "\000" >>forged2 &&
	git hash-object -w -t commit forged2 >forged2.commit &&
	git show --pretty=short --show-signature $(cat forged2.commit) >actual2 &&
	grep "BAD signature from" actual2 &&
	! grep "Good signature from" actual2
'

test_expect_success GPG 'amending already signed commit' '
	git checkout fourth-signed^0 &&
	git commit --amend -S --no-edit &&
	git show -s --show-signature HEAD >actual &&
	grep "Good signature from" actual &&
	! grep "BAD signature from" actual
'

test_done
