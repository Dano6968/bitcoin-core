#!/usr/bin/env python3
# Copyright (c) 2020 The PIVX developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
"""
Test checking:
 1) pre-v6 multi-block payments
 2) different payment ordering validation (proposals paid in different order)
 3) duplicated payments validation --> wrong path
 4) post-v6 single block payments
 5) single block payments ordering validation (proposals paid in different order)
 6) duplicated payments validation --> wrong path
"""

import time

from test_framework.test_framework import PivxTier2TestFramework
from test_framework.util import (
    assert_equal
)
from test_framework.budget_util import (
    check_budget_finalization_sync,
    create_proposals_tx,
    check_budgetprojection,
    check_proposal_existence,
    check_mn_list,
    check_mns_status_legacy,
    check_mns_status,
    check_vote_existence,
    get_proposal,
    Proposal,
    propagate_proposals
)

class BudgetTest(PivxTier2TestFramework):

    def set_test_params(self):
        super().set_test_params(300)

    def broadcastbudgetfinalization(self, node, with_ping_mns=[]):
        self.log.info("suggesting the budget finalization..")
        assert (node.mnfinalbudgetsuggest() is not None)

        self.log.info("confirming the budget finalization..")
        time.sleep(1)
        self.stake(4, with_ping_mns)

        self.log.info("broadcasting the budget finalization..")
        return node.mnfinalbudgetsuggest()

    def submit_proposals(self, props):
        props = create_proposals_tx(self.miner, props)
        # generate 3 blocks to confirm the tx (and update the mnping)
        self.stake(3, [self.remoteOne, self.remoteTwo])
        # check fee tx existence
        for entry in props:
            txinfo = self.miner.gettransaction(entry.feeTxId)
            assert_equal(txinfo['amount'], -50.00)
        # propagate proposals
        props = propagate_proposals(self.miner, props)
        # let's wait a little bit and see if all nodes are sync
        time.sleep(1)
        for entry in props:
            check_proposal_existence(self.nodes, entry.name, entry.proposalHash)
            self.log.info("proposal %s broadcast successful!" % entry.name)
        return props

    def vote_legacy(self, node_voter, proposal, vote_direction, mn_voter_alias):
        self.log.info("Voting with " + mn_voter_alias)
        voteResult = node_voter.mnbudgetvote("alias", proposal.proposalHash, vote_direction, mn_voter_alias, True)
        assert_equal(voteResult["detail"][0]["result"], "success")
        time.sleep(1)

    def vote(self, node_voter, proposal, vote_direction, pro_reg_tx):
        self.log.info("Voting with DMN " + pro_reg_tx)
        voteResult = node_voter.mnbudgetvote("alias", proposal.proposalHash, vote_direction, pro_reg_tx)
        assert_equal(voteResult["detail"][0]["result"], "success")
        time.sleep(1)

    def vote_finalization(self, voting_node, nodes, budget_fin_hash, legacy, votes_count):
        voteResult = voting_node.mnfinalbudget("vote-many" if legacy else "vote", budget_fin_hash, legacy)
        assert_equal(voteResult["detail"][0]["result"], "success")
        time.sleep(2) # wait a bit
        check_budget_finalization_sync(nodes, votes_count, "OK")

    def run_test(self):
        self.enable_mocktime()
        self.setup_3_masternodes_network()
        txHashSet = set([self.mnOneCollateral.hash, self.mnTwoCollateral.hash, self.proRegTx1])
        # check mn list from miner
        check_mn_list(self.miner, txHashSet)

        # check status of masternodes
        check_mns_status_legacy(self.remoteOne, self.mnOneCollateral.hash)
        check_mns_status_legacy(self.remoteTwo, self.mnTwoCollateral.hash)
        check_mns_status(self.remoteDMN1, self.proRegTx1)

        # activate sporks
        self.activate_spork(self.minerPos, "SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT")
        self.activate_spork(self.minerPos, "SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT")
        self.activate_spork(self.minerPos, "SPORK_13_ENABLE_SUPERBLOCKS")
        nextSuperBlockHeight = self.miner.getnextsuperblock()

        # Submit first proposal
        self.log.info("preparing budget proposal..")
        # Create 15 more proposals to have a higher tier two net gossip movement
        props = []
        for i in range(16):
            props.append(Proposal("prop_"+str(i),
                         "https://link_"+str(i)+".com",
                         3,
                         self.miner.getnewaddress(),
                         11 * (i + 1)))
        self.submit_proposals(props)

        # Proposals are established after 5 minutes. Mine 7 blocks
        # Proposal needs to be on the chain > 5 min.
        self.stake(7, [self.remoteOne, self.remoteTwo])
        # Check proposals existence
        for i in range(self.num_nodes):
            assert_equal(len(self.nodes[i].getbudgetinfo()), 16)

        # now let's vote for the two first proposals
        expected_budget = []
        blockStart = nextSuperBlockHeight
        alloted = 0
        for i in range(2):
            prop = props[i]
            self.vote_legacy(self.ownerOne, prop, "yes", self.masternodeOneAlias)
            check_vote_existence(self.nodes, prop.name, self.mnOneCollateral.hash, "YES", True)
            self.vote_legacy(self.ownerTwo, prop, "yes", self.masternodeTwoAlias)
            check_vote_existence(self.nodes, prop.name, self.mnTwoCollateral.hash, "YES", True)
            if i < 1:
                self.vote(self.ownerOne, prop, "yes", self.proRegTx1)
                check_vote_existence(self.nodes, prop.name, self.proRegTx1, "YES", True)
            alloted += prop.amountPerCycle
            expected_budget.append(get_proposal(prop, blockStart, prop.amountPerCycle, alloted, 3 - i))

        # Now check the budget
        check_budgetprojection(self.nodes, expected_budget, self.log)

        # Quick block count check.
        assert_equal(self.ownerOne.getblockcount(), 273)
        self.stake(9, [self.remoteOne, self.remoteTwo])
        # suggest the budget finalization and confirm the tx (+4 blocks).
        budgetFinHash = self.broadcastbudgetfinalization(self.miner,
                                                         with_ping_mns=[self.remoteOne, self.remoteTwo])
        assert (budgetFinHash != "")
        time.sleep(2)

        self.log.info("voting budget finalization..")
        self.vote_finalization(self.ownerOne, self.nodes, budgetFinHash, True, 1)
        self.vote_finalization(self.ownerTwo, self.nodes, budgetFinHash, True, 2)
        self.vote_finalization(self.remoteDMN1, self.nodes, budgetFinHash, False, 3)
        check_budget_finalization_sync(self.nodes, 3, "OK")

        self.stake(6, [self.remoteOne, self.remoteTwo])

        print(self.miner.getblockcount())

        for i in range(2):
            prop = props[i]
            addrInfo = self.miner.listreceivedbyaddress(0, False, False, prop.paymentAddr)
            assert_equal(addrInfo[0]["amount"], prop.amountPerCycle)
        self.log.info("pre-v6 budget proposal paid, all good")

        # Check that the proposal info returns updated payment count
        expected_budget[0]["RemainingPaymentCount"] -= 1
        expected_budget[1]["RemainingPaymentCount"] -= 1
        check_budgetprojection(self.nodes, expected_budget, self.log)
        self.stake(1, [self.remoteOne, self.remoteTwo])

        # Now test post enforcement
        nextSuperBlockHeight = self.miner.getnextsuperblock()



        self.log.info("All good.")

if __name__ == '__main__':
    BudgetTest().main()
